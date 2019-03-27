//===-- Editline.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <iomanip>
#include <iostream>
#include <limits.h>

#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/Host/Editline.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/SelectHelper.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StringList.h"
#include "lldb/Utility/Timeout.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Threading.h"

using namespace lldb_private;
using namespace lldb_private::line_editor;

// Workaround for what looks like an OS X-specific issue, but other platforms
// may benefit from something similar if issues arise.  The libedit library
// doesn't explicitly initialize the curses termcap library, which it gets away
// with until TERM is set to VT100 where it stumbles over an implementation
// assumption that may not exist on other platforms.  The setupterm() function
// would normally require headers that don't work gracefully in this context,
// so the function declaraction has been hoisted here.
#if defined(__APPLE__)
extern "C" {
int setupterm(char *term, int fildes, int *errret);
}
#define USE_SETUPTERM_WORKAROUND
#endif

// Editline uses careful cursor management to achieve the illusion of editing a
// multi-line block of text with a single line editor.  Preserving this
// illusion requires fairly careful management of cursor state.  Read and
// understand the relationship between DisplayInput(), MoveCursor(),
// SetCurrentLine(), and SaveEditedLine() before making changes.

#define ESCAPE "\x1b"
#define ANSI_FAINT ESCAPE "[2m"
#define ANSI_UNFAINT ESCAPE "[22m"
#define ANSI_CLEAR_BELOW ESCAPE "[J"
#define ANSI_CLEAR_RIGHT ESCAPE "[K"
#define ANSI_SET_COLUMN_N ESCAPE "[%dG"
#define ANSI_UP_N_ROWS ESCAPE "[%dA"
#define ANSI_DOWN_N_ROWS ESCAPE "[%dB"

#if LLDB_EDITLINE_USE_WCHAR

#define EditLineConstString(str) L##str
#define EditLineStringFormatSpec "%ls"

#else

#define EditLineConstString(str) str
#define EditLineStringFormatSpec "%s"

// use #defines so wide version functions and structs will resolve to old
// versions for case of libedit not built with wide char support
#define history_w history
#define history_winit history_init
#define history_wend history_end
#define HistoryW History
#define HistEventW HistEvent
#define LineInfoW LineInfo

#define el_wgets el_gets
#define el_wgetc el_getc
#define el_wpush el_push
#define el_wparse el_parse
#define el_wset el_set
#define el_wget el_get
#define el_wline el_line
#define el_winsertstr el_insertstr
#define el_wdeletestr el_deletestr

#endif // #if LLDB_EDITLINE_USE_WCHAR

bool IsOnlySpaces(const EditLineStringType &content) {
  for (wchar_t ch : content) {
    if (ch != EditLineCharType(' '))
      return false;
  }
  return true;
}

EditLineStringType CombineLines(const std::vector<EditLineStringType> &lines) {
  EditLineStringStreamType combined_stream;
  for (EditLineStringType line : lines) {
    combined_stream << line.c_str() << "\n";
  }
  return combined_stream.str();
}

std::vector<EditLineStringType> SplitLines(const EditLineStringType &input) {
  std::vector<EditLineStringType> result;
  size_t start = 0;
  while (start < input.length()) {
    size_t end = input.find('\n', start);
    if (end == std::string::npos) {
      result.insert(result.end(), input.substr(start));
      break;
    }
    result.insert(result.end(), input.substr(start, end - start));
    start = end + 1;
  }
  return result;
}

EditLineStringType FixIndentation(const EditLineStringType &line,
                                  int indent_correction) {
  if (indent_correction == 0)
    return line;
  if (indent_correction < 0)
    return line.substr(-indent_correction);
  return EditLineStringType(indent_correction, EditLineCharType(' ')) + line;
}

int GetIndentation(const EditLineStringType &line) {
  int space_count = 0;
  for (EditLineCharType ch : line) {
    if (ch != EditLineCharType(' '))
      break;
    ++space_count;
  }
  return space_count;
}

bool IsInputPending(FILE *file) {
  // FIXME: This will be broken on Windows if we ever re-enable Editline.  You
  // can't use select
  // on something that isn't a socket.  This will have to be re-written to not
  // use a FILE*, but instead use some kind of yet-to-be-created abstraction
  // that select-like functionality on non-socket objects.
  const int fd = fileno(file);
  SelectHelper select_helper;
  select_helper.SetTimeout(std::chrono::microseconds(0));
  select_helper.FDSetRead(fd);
  return select_helper.Select().Success();
}

namespace lldb_private {
namespace line_editor {
typedef std::weak_ptr<EditlineHistory> EditlineHistoryWP;

// EditlineHistory objects are sometimes shared between multiple Editline
// instances with the same program name.

class EditlineHistory {
private:
  // Use static GetHistory() function to get a EditlineHistorySP to one of
  // these objects
  EditlineHistory(const std::string &prefix, uint32_t size, bool unique_entries)
      : m_history(NULL), m_event(), m_prefix(prefix), m_path() {
    m_history = history_winit();
    history_w(m_history, &m_event, H_SETSIZE, size);
    if (unique_entries)
      history_w(m_history, &m_event, H_SETUNIQUE, 1);
  }

  const char *GetHistoryFilePath() {
    if (m_path.empty() && m_history && !m_prefix.empty()) {
      FileSpec parent_path("~/.lldb");
      FileSystem::Instance().Resolve(parent_path);
      char history_path[PATH_MAX];
      if (!llvm::sys::fs::create_directory(parent_path.GetPath())) {
        snprintf(history_path, sizeof(history_path), "~/.lldb/%s-history",
                 m_prefix.c_str());
      } else {
        snprintf(history_path, sizeof(history_path), "~/%s-widehistory",
                 m_prefix.c_str());
      }
      auto file_spec = FileSpec(history_path);
      FileSystem::Instance().Resolve(file_spec);
      m_path = file_spec.GetPath();
    }
    if (m_path.empty())
      return NULL;
    return m_path.c_str();
  }

public:
  ~EditlineHistory() {
    Save();

    if (m_history) {
      history_wend(m_history);
      m_history = NULL;
    }
  }

  static EditlineHistorySP GetHistory(const std::string &prefix) {
    typedef std::map<std::string, EditlineHistoryWP> WeakHistoryMap;
    static std::recursive_mutex g_mutex;
    static WeakHistoryMap g_weak_map;
    std::lock_guard<std::recursive_mutex> guard(g_mutex);
    WeakHistoryMap::const_iterator pos = g_weak_map.find(prefix);
    EditlineHistorySP history_sp;
    if (pos != g_weak_map.end()) {
      history_sp = pos->second.lock();
      if (history_sp)
        return history_sp;
      g_weak_map.erase(pos);
    }
    history_sp.reset(new EditlineHistory(prefix, 800, true));
    g_weak_map[prefix] = history_sp;
    return history_sp;
  }

  bool IsValid() const { return m_history != NULL; }

  HistoryW *GetHistoryPtr() { return m_history; }

  void Enter(const EditLineCharType *line_cstr) {
    if (m_history)
      history_w(m_history, &m_event, H_ENTER, line_cstr);
  }

  bool Load() {
    if (m_history) {
      const char *path = GetHistoryFilePath();
      if (path) {
        history_w(m_history, &m_event, H_LOAD, path);
        return true;
      }
    }
    return false;
  }

  bool Save() {
    if (m_history) {
      const char *path = GetHistoryFilePath();
      if (path) {
        history_w(m_history, &m_event, H_SAVE, path);
        return true;
      }
    }
    return false;
  }

protected:
  HistoryW *m_history; // The history object
  HistEventW m_event;  // The history event needed to contain all history events
  std::string m_prefix; // The prefix name (usually the editline program name)
                        // to use when loading/saving history
  std::string m_path;   // Path to the history file
};
}
}

//------------------------------------------------------------------
// Editline private methods
//------------------------------------------------------------------

void Editline::SetBaseLineNumber(int line_number) {
  std::stringstream line_number_stream;
  line_number_stream << line_number;
  m_base_line_number = line_number;
  m_line_number_digits =
      std::max(3, (int)line_number_stream.str().length() + 1);
}

std::string Editline::PromptForIndex(int line_index) {
  bool use_line_numbers = m_multiline_enabled && m_base_line_number > 0;
  std::string prompt = m_set_prompt;
  if (use_line_numbers && prompt.length() == 0) {
    prompt = ": ";
  }
  std::string continuation_prompt = prompt;
  if (m_set_continuation_prompt.length() > 0) {
    continuation_prompt = m_set_continuation_prompt;

    // Ensure that both prompts are the same length through space padding
    while (continuation_prompt.length() < prompt.length()) {
      continuation_prompt += ' ';
    }
    while (prompt.length() < continuation_prompt.length()) {
      prompt += ' ';
    }
  }

  if (use_line_numbers) {
    StreamString prompt_stream;
    prompt_stream.Printf(
        "%*d%s", m_line_number_digits, m_base_line_number + line_index,
        (line_index == 0) ? prompt.c_str() : continuation_prompt.c_str());
    return std::move(prompt_stream.GetString());
  }
  return (line_index == 0) ? prompt : continuation_prompt;
}

void Editline::SetCurrentLine(int line_index) {
  m_current_line_index = line_index;
  m_current_prompt = PromptForIndex(line_index);
}

int Editline::GetPromptWidth() { return (int)PromptForIndex(0).length(); }

bool Editline::IsEmacs() {
  const char *editor;
  el_get(m_editline, EL_EDITOR, &editor);
  return editor[0] == 'e';
}

bool Editline::IsOnlySpaces() {
  const LineInfoW *info = el_wline(m_editline);
  for (const EditLineCharType *character = info->buffer;
       character < info->lastchar; character++) {
    if (*character != ' ')
      return false;
  }
  return true;
}

int Editline::GetLineIndexForLocation(CursorLocation location, int cursor_row) {
  int line = 0;
  if (location == CursorLocation::EditingPrompt ||
      location == CursorLocation::BlockEnd ||
      location == CursorLocation::EditingCursor) {
    for (unsigned index = 0; index < m_current_line_index; index++) {
      line += CountRowsForLine(m_input_lines[index]);
    }
    if (location == CursorLocation::EditingCursor) {
      line += cursor_row;
    } else if (location == CursorLocation::BlockEnd) {
      for (unsigned index = m_current_line_index; index < m_input_lines.size();
           index++) {
        line += CountRowsForLine(m_input_lines[index]);
      }
      --line;
    }
  }
  return line;
}

void Editline::MoveCursor(CursorLocation from, CursorLocation to) {
  const LineInfoW *info = el_wline(m_editline);
  int editline_cursor_position =
      (int)((info->cursor - info->buffer) + GetPromptWidth());
  int editline_cursor_row = editline_cursor_position / m_terminal_width;

  // Determine relative starting and ending lines
  int fromLine = GetLineIndexForLocation(from, editline_cursor_row);
  int toLine = GetLineIndexForLocation(to, editline_cursor_row);
  if (toLine != fromLine) {
    fprintf(m_output_file,
            (toLine > fromLine) ? ANSI_DOWN_N_ROWS : ANSI_UP_N_ROWS,
            std::abs(toLine - fromLine));
  }

  // Determine target column
  int toColumn = 1;
  if (to == CursorLocation::EditingCursor) {
    toColumn =
        editline_cursor_position - (editline_cursor_row * m_terminal_width) + 1;
  } else if (to == CursorLocation::BlockEnd && !m_input_lines.empty()) {
    toColumn =
        ((m_input_lines[m_input_lines.size() - 1].length() + GetPromptWidth()) %
         80) +
        1;
  }
  fprintf(m_output_file, ANSI_SET_COLUMN_N, toColumn);
}

void Editline::DisplayInput(int firstIndex) {
  fprintf(m_output_file, ANSI_SET_COLUMN_N ANSI_CLEAR_BELOW, 1);
  int line_count = (int)m_input_lines.size();
  const char *faint = m_color_prompts ? ANSI_FAINT : "";
  const char *unfaint = m_color_prompts ? ANSI_UNFAINT : "";

  for (int index = firstIndex; index < line_count; index++) {
    fprintf(m_output_file, "%s"
                           "%s"
                           "%s" EditLineStringFormatSpec " ",
            faint, PromptForIndex(index).c_str(), unfaint,
            m_input_lines[index].c_str());
    if (index < line_count - 1)
      fprintf(m_output_file, "\n");
  }
}

int Editline::CountRowsForLine(const EditLineStringType &content) {
  auto prompt =
      PromptForIndex(0); // Prompt width is constant during an edit session
  int line_length = (int)(content.length() + prompt.length());
  return (line_length / m_terminal_width) + 1;
}

void Editline::SaveEditedLine() {
  const LineInfoW *info = el_wline(m_editline);
  m_input_lines[m_current_line_index] =
      EditLineStringType(info->buffer, info->lastchar - info->buffer);
}

StringList Editline::GetInputAsStringList(int line_count) {
  StringList lines;
  for (EditLineStringType line : m_input_lines) {
    if (line_count == 0)
      break;
#if LLDB_EDITLINE_USE_WCHAR
    lines.AppendString(m_utf8conv.to_bytes(line));
#else
    lines.AppendString(line);
#endif
    --line_count;
  }
  return lines;
}

unsigned char Editline::RecallHistory(bool earlier) {
  if (!m_history_sp || !m_history_sp->IsValid())
    return CC_ERROR;

  HistoryW *pHistory = m_history_sp->GetHistoryPtr();
  HistEventW history_event;
  std::vector<EditLineStringType> new_input_lines;

  // Treat moving from the "live" entry differently
  if (!m_in_history) {
    if (!earlier)
      return CC_ERROR; // Can't go newer than the "live" entry
    if (history_w(pHistory, &history_event, H_FIRST) == -1)
      return CC_ERROR;

    // Save any edits to the "live" entry in case we return by moving forward
    // in history (it would be more bash-like to save over any current entry,
    // but libedit doesn't offer the ability to add entries anywhere except the
    // end.)
    SaveEditedLine();
    m_live_history_lines = m_input_lines;
    m_in_history = true;
  } else {
    if (history_w(pHistory, &history_event, earlier ? H_PREV : H_NEXT) == -1) {
      // Can't move earlier than the earliest entry
      if (earlier)
        return CC_ERROR;

      // ... but moving to newer than the newest yields the "live" entry
      new_input_lines = m_live_history_lines;
      m_in_history = false;
    }
  }

  // If we're pulling the lines from history, split them apart
  if (m_in_history)
    new_input_lines = SplitLines(history_event.str);

  // Erase the current edit session and replace it with a new one
  MoveCursor(CursorLocation::EditingCursor, CursorLocation::BlockStart);
  m_input_lines = new_input_lines;
  DisplayInput();

  // Prepare to edit the last line when moving to previous entry, or the first
  // line when moving to next entry
  SetCurrentLine(m_current_line_index =
                     earlier ? (int)m_input_lines.size() - 1 : 0);
  MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingPrompt);
  return CC_NEWLINE;
}

int Editline::GetCharacter(EditLineGetCharType *c) {
  const LineInfoW *info = el_wline(m_editline);

  // Paint a faint version of the desired prompt over the version libedit draws
  // (will only be requested if colors are supported)
  if (m_needs_prompt_repaint) {
    MoveCursor(CursorLocation::EditingCursor, CursorLocation::EditingPrompt);
    fprintf(m_output_file, "%s"
                           "%s"
                           "%s",
            ANSI_FAINT, Prompt(), ANSI_UNFAINT);
    MoveCursor(CursorLocation::EditingPrompt, CursorLocation::EditingCursor);
    m_needs_prompt_repaint = false;
  }

  if (m_multiline_enabled) {
    // Detect when the number of rows used for this input line changes due to
    // an edit
    int lineLength = (int)((info->lastchar - info->buffer) + GetPromptWidth());
    int new_line_rows = (lineLength / m_terminal_width) + 1;
    if (m_current_line_rows != -1 && new_line_rows != m_current_line_rows) {
      // Respond by repainting the current state from this line on
      MoveCursor(CursorLocation::EditingCursor, CursorLocation::EditingPrompt);
      SaveEditedLine();
      DisplayInput(m_current_line_index);
      MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingCursor);
    }
    m_current_line_rows = new_line_rows;
  }

  // Read an actual character
  while (true) {
    lldb::ConnectionStatus status = lldb::eConnectionStatusSuccess;
    char ch = 0;

    // This mutex is locked by our caller (GetLine). Unlock it while we read a
    // character (blocking operation), so we do not hold the mutex
    // indefinitely. This gives a chance for someone to interrupt us. After
    // Read returns, immediately lock the mutex again and check if we were
    // interrupted.
    m_output_mutex.unlock();
    int read_count = m_input_connection.Read(&ch, 1, llvm::None, status, NULL);
    m_output_mutex.lock();
    if (m_editor_status == EditorStatus::Interrupted) {
      while (read_count > 0 && status == lldb::eConnectionStatusSuccess)
        read_count = m_input_connection.Read(&ch, 1, llvm::None, status, NULL);
      lldbassert(status == lldb::eConnectionStatusInterrupted);
      return 0;
    }

    if (read_count) {
      if (CompleteCharacter(ch, *c))
        return 1;
    } else {
      switch (status) {
      case lldb::eConnectionStatusSuccess: // Success
        break;

      case lldb::eConnectionStatusInterrupted:
        llvm_unreachable("Interrupts should have been handled above.");

      case lldb::eConnectionStatusError:        // Check GetError() for details
      case lldb::eConnectionStatusTimedOut:     // Request timed out
      case lldb::eConnectionStatusEndOfFile:    // End-of-file encountered
      case lldb::eConnectionStatusNoConnection: // No connection
      case lldb::eConnectionStatusLostConnection: // Lost connection while
                                                  // connected to a valid
                                                  // connection
        m_editor_status = EditorStatus::EndOfInput;
        return 0;
      }
    }
  }
}

const char *Editline::Prompt() {
  if (m_color_prompts)
    m_needs_prompt_repaint = true;
  return m_current_prompt.c_str();
}

unsigned char Editline::BreakLineCommand(int ch) {
  // Preserve any content beyond the cursor, truncate and save the current line
  const LineInfoW *info = el_wline(m_editline);
  auto current_line =
      EditLineStringType(info->buffer, info->cursor - info->buffer);
  auto new_line_fragment =
      EditLineStringType(info->cursor, info->lastchar - info->cursor);
  m_input_lines[m_current_line_index] = current_line;

  // Ignore whitespace-only extra fragments when breaking a line
  if (::IsOnlySpaces(new_line_fragment))
    new_line_fragment = EditLineConstString("");

  // Establish the new cursor position at the start of a line when inserting a
  // line break
  m_revert_cursor_index = 0;

  // Don't perform automatic formatting when pasting
  if (!IsInputPending(m_input_file)) {
    // Apply smart indentation
    if (m_fix_indentation_callback) {
      StringList lines = GetInputAsStringList(m_current_line_index + 1);
#if LLDB_EDITLINE_USE_WCHAR
      lines.AppendString(m_utf8conv.to_bytes(new_line_fragment));
#else
      lines.AppendString(new_line_fragment);
#endif

      int indent_correction = m_fix_indentation_callback(
          this, lines, 0, m_fix_indentation_callback_baton);
      new_line_fragment = FixIndentation(new_line_fragment, indent_correction);
      m_revert_cursor_index = GetIndentation(new_line_fragment);
    }
  }

  // Insert the new line and repaint everything from the split line on down
  m_input_lines.insert(m_input_lines.begin() + m_current_line_index + 1,
                       new_line_fragment);
  MoveCursor(CursorLocation::EditingCursor, CursorLocation::EditingPrompt);
  DisplayInput(m_current_line_index);

  // Reposition the cursor to the right line and prepare to edit the new line
  SetCurrentLine(m_current_line_index + 1);
  MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingPrompt);
  return CC_NEWLINE;
}

unsigned char Editline::EndOrAddLineCommand(int ch) {
  // Don't perform end of input detection when pasting, always treat this as a
  // line break
  if (IsInputPending(m_input_file)) {
    return BreakLineCommand(ch);
  }

  // Save any edits to this line
  SaveEditedLine();

  // If this is the end of the last line, consider whether to add a line
  // instead
  const LineInfoW *info = el_wline(m_editline);
  if (m_current_line_index == m_input_lines.size() - 1 &&
      info->cursor == info->lastchar) {
    if (m_is_input_complete_callback) {
      auto lines = GetInputAsStringList();
      if (!m_is_input_complete_callback(this, lines,
                                        m_is_input_complete_callback_baton)) {
        return BreakLineCommand(ch);
      }

      // The completion test is allowed to change the input lines when complete
      m_input_lines.clear();
      for (unsigned index = 0; index < lines.GetSize(); index++) {
#if LLDB_EDITLINE_USE_WCHAR
        m_input_lines.insert(m_input_lines.end(),
                             m_utf8conv.from_bytes(lines[index]));
#else
        m_input_lines.insert(m_input_lines.end(), lines[index]);
#endif
      }
    }
  }
  MoveCursor(CursorLocation::EditingCursor, CursorLocation::BlockEnd);
  fprintf(m_output_file, "\n");
  m_editor_status = EditorStatus::Complete;
  return CC_NEWLINE;
}

unsigned char Editline::DeleteNextCharCommand(int ch) {
  LineInfoW *info = const_cast<LineInfoW *>(el_wline(m_editline));

  // Just delete the next character normally if possible
  if (info->cursor < info->lastchar) {
    info->cursor++;
    el_deletestr(m_editline, 1);
    return CC_REFRESH;
  }

  // Fail when at the end of the last line, except when ^D is pressed on the
  // line is empty, in which case it is treated as EOF
  if (m_current_line_index == m_input_lines.size() - 1) {
    if (ch == 4 && info->buffer == info->lastchar) {
      fprintf(m_output_file, "^D\n");
      m_editor_status = EditorStatus::EndOfInput;
      return CC_EOF;
    }
    return CC_ERROR;
  }

  // Prepare to combine this line with the one below
  MoveCursor(CursorLocation::EditingCursor, CursorLocation::EditingPrompt);

  // Insert the next line of text at the cursor and restore the cursor position
  const EditLineCharType *cursor = info->cursor;
  el_winsertstr(m_editline, m_input_lines[m_current_line_index + 1].c_str());
  info->cursor = cursor;
  SaveEditedLine();

  // Delete the extra line
  m_input_lines.erase(m_input_lines.begin() + m_current_line_index + 1);

  // Clear and repaint from this line on down
  DisplayInput(m_current_line_index);
  MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingCursor);
  return CC_REFRESH;
}

unsigned char Editline::DeletePreviousCharCommand(int ch) {
  LineInfoW *info = const_cast<LineInfoW *>(el_wline(m_editline));

  // Just delete the previous character normally when not at the start of a
  // line
  if (info->cursor > info->buffer) {
    el_deletestr(m_editline, 1);
    return CC_REFRESH;
  }

  // No prior line and no prior character?  Let the user know
  if (m_current_line_index == 0)
    return CC_ERROR;

  // No prior character, but prior line?  Combine with the line above
  SaveEditedLine();
  SetCurrentLine(m_current_line_index - 1);
  auto priorLine = m_input_lines[m_current_line_index];
  m_input_lines.erase(m_input_lines.begin() + m_current_line_index);
  m_input_lines[m_current_line_index] =
      priorLine + m_input_lines[m_current_line_index];

  // Repaint from the new line down
  fprintf(m_output_file, ANSI_UP_N_ROWS ANSI_SET_COLUMN_N,
          CountRowsForLine(priorLine), 1);
  DisplayInput(m_current_line_index);

  // Put the cursor back where libedit expects it to be before returning to
  // editing by telling libedit about the newly inserted text
  MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingPrompt);
  el_winsertstr(m_editline, priorLine.c_str());
  return CC_REDISPLAY;
}

unsigned char Editline::PreviousLineCommand(int ch) {
  SaveEditedLine();

  if (m_current_line_index == 0) {
    return RecallHistory(true);
  }

  // Start from a known location
  MoveCursor(CursorLocation::EditingCursor, CursorLocation::EditingPrompt);

  // Treat moving up from a blank last line as a deletion of that line
  if (m_current_line_index == m_input_lines.size() - 1 && IsOnlySpaces()) {
    m_input_lines.erase(m_input_lines.begin() + m_current_line_index);
    fprintf(m_output_file, ANSI_CLEAR_BELOW);
  }

  SetCurrentLine(m_current_line_index - 1);
  fprintf(m_output_file, ANSI_UP_N_ROWS ANSI_SET_COLUMN_N,
          CountRowsForLine(m_input_lines[m_current_line_index]), 1);
  return CC_NEWLINE;
}

unsigned char Editline::NextLineCommand(int ch) {
  SaveEditedLine();

  // Handle attempts to move down from the last line
  if (m_current_line_index == m_input_lines.size() - 1) {
    // Don't add an extra line if the existing last line is blank, move through
    // history instead
    if (IsOnlySpaces()) {
      return RecallHistory(false);
    }

    // Determine indentation for the new line
    int indentation = 0;
    if (m_fix_indentation_callback) {
      StringList lines = GetInputAsStringList();
      lines.AppendString("");
      indentation = m_fix_indentation_callback(
          this, lines, 0, m_fix_indentation_callback_baton);
    }
    m_input_lines.insert(
        m_input_lines.end(),
        EditLineStringType(indentation, EditLineCharType(' ')));
  }

  // Move down past the current line using newlines to force scrolling if
  // needed
  SetCurrentLine(m_current_line_index + 1);
  const LineInfoW *info = el_wline(m_editline);
  int cursor_position = (int)((info->cursor - info->buffer) + GetPromptWidth());
  int cursor_row = cursor_position / m_terminal_width;
  for (int line_count = 0; line_count < m_current_line_rows - cursor_row;
       line_count++) {
    fprintf(m_output_file, "\n");
  }
  return CC_NEWLINE;
}

unsigned char Editline::PreviousHistoryCommand(int ch) {
  SaveEditedLine();

  return RecallHistory(true);
}

unsigned char Editline::NextHistoryCommand(int ch) {
  SaveEditedLine();

  return RecallHistory(false);
}

unsigned char Editline::FixIndentationCommand(int ch) {
  if (!m_fix_indentation_callback)
    return CC_NORM;

  // Insert the character typed before proceeding
  EditLineCharType inserted[] = {(EditLineCharType)ch, 0};
  el_winsertstr(m_editline, inserted);
  LineInfoW *info = const_cast<LineInfoW *>(el_wline(m_editline));
  int cursor_position = info->cursor - info->buffer;

  // Save the edits and determine the correct indentation level
  SaveEditedLine();
  StringList lines = GetInputAsStringList(m_current_line_index + 1);
  int indent_correction = m_fix_indentation_callback(
      this, lines, cursor_position, m_fix_indentation_callback_baton);

  // If it is already correct no special work is needed
  if (indent_correction == 0)
    return CC_REFRESH;

  // Change the indentation level of the line
  std::string currentLine = lines.GetStringAtIndex(m_current_line_index);
  if (indent_correction > 0) {
    currentLine = currentLine.insert(0, indent_correction, ' ');
  } else {
    currentLine = currentLine.erase(0, -indent_correction);
  }
#if LLDB_EDITLINE_USE_WCHAR
  m_input_lines[m_current_line_index] = m_utf8conv.from_bytes(currentLine);
#else
  m_input_lines[m_current_line_index] = currentLine;
#endif

  // Update the display to reflect the change
  MoveCursor(CursorLocation::EditingCursor, CursorLocation::EditingPrompt);
  DisplayInput(m_current_line_index);

  // Reposition the cursor back on the original line and prepare to restart
  // editing with a new cursor position
  SetCurrentLine(m_current_line_index);
  MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingPrompt);
  m_revert_cursor_index = cursor_position + indent_correction;
  return CC_NEWLINE;
}

unsigned char Editline::RevertLineCommand(int ch) {
  el_winsertstr(m_editline, m_input_lines[m_current_line_index].c_str());
  if (m_revert_cursor_index >= 0) {
    LineInfoW *info = const_cast<LineInfoW *>(el_wline(m_editline));
    info->cursor = info->buffer + m_revert_cursor_index;
    if (info->cursor > info->lastchar) {
      info->cursor = info->lastchar;
    }
    m_revert_cursor_index = -1;
  }
  return CC_REFRESH;
}

unsigned char Editline::BufferStartCommand(int ch) {
  SaveEditedLine();
  MoveCursor(CursorLocation::EditingCursor, CursorLocation::BlockStart);
  SetCurrentLine(0);
  m_revert_cursor_index = 0;
  return CC_NEWLINE;
}

unsigned char Editline::BufferEndCommand(int ch) {
  SaveEditedLine();
  MoveCursor(CursorLocation::EditingCursor, CursorLocation::BlockEnd);
  SetCurrentLine((int)m_input_lines.size() - 1);
  MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingPrompt);
  return CC_NEWLINE;
}

//------------------------------------------------------------------------------
/// Prints completions and their descriptions to the given file. Only the
/// completions in the interval [start, end) are printed.
//------------------------------------------------------------------------------
static void PrintCompletion(FILE *output_file, size_t start, size_t end,
                            StringList &completions, StringList &descriptions) {
  // This is an 'int' because of printf.
  int max_len = 0;

  for (size_t i = start; i < end; i++) {
    const char *completion_str = completions.GetStringAtIndex(i);
    max_len = std::max((int)strlen(completion_str), max_len);
  }

  for (size_t i = start; i < end; i++) {
    const char *completion_str = completions.GetStringAtIndex(i);
    const char *description_str = descriptions.GetStringAtIndex(i);

    fprintf(output_file, "\n\t%-*s", max_len, completion_str);

    // Print the description if we got one.
    if (strlen(description_str))
      fprintf(output_file, " -- %s", description_str);
  }
}

unsigned char Editline::TabCommand(int ch) {
  if (m_completion_callback == nullptr)
    return CC_ERROR;

  const LineInfo *line_info = el_line(m_editline);
  StringList completions, descriptions;
  int page_size = 40;

  const int num_completions = m_completion_callback(
      line_info->buffer, line_info->cursor, line_info->lastchar,
      0,  // Don't skip any matches (start at match zero)
      -1, // Get all the matches
      completions, descriptions, m_completion_callback_baton);

  if (num_completions == 0)
    return CC_ERROR;
  //    if (num_completions == -1)
  //    {
  //        el_insertstr (m_editline, m_completion_key);
  //        return CC_REDISPLAY;
  //    }
  //    else
  if (num_completions == -2) {
    // Replace the entire line with the first string...
    el_deletestr(m_editline, line_info->cursor - line_info->buffer);
    el_insertstr(m_editline, completions.GetStringAtIndex(0));
    return CC_REDISPLAY;
  }

  // If we get a longer match display that first.
  const char *completion_str = completions.GetStringAtIndex(0);
  if (completion_str != nullptr && *completion_str != '\0') {
    el_insertstr(m_editline, completion_str);
    return CC_REDISPLAY;
  }

  if (num_completions > 1) {
    int num_elements = num_completions + 1;
    fprintf(m_output_file, "\n" ANSI_CLEAR_BELOW "Available completions:");
    if (num_completions < page_size) {
      PrintCompletion(m_output_file, 1, num_elements, completions,
                      descriptions);
      fprintf(m_output_file, "\n");
    } else {
      int cur_pos = 1;
      char reply;
      int got_char;
      while (cur_pos < num_elements) {
        int endpoint = cur_pos + page_size;
        if (endpoint > num_elements)
          endpoint = num_elements;

        PrintCompletion(m_output_file, cur_pos, endpoint, completions,
                        descriptions);
        cur_pos = endpoint;

        if (cur_pos >= num_elements) {
          fprintf(m_output_file, "\n");
          break;
        }

        fprintf(m_output_file, "\nMore (Y/n/a): ");
        reply = 'n';
        got_char = el_getc(m_editline, &reply);
        if (got_char == -1 || reply == 'n')
          break;
        if (reply == 'a')
          page_size = num_elements - cur_pos;
      }
    }
    DisplayInput();
    MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingCursor);
  }
  return CC_REDISPLAY;
}

void Editline::ConfigureEditor(bool multiline) {
  if (m_editline && m_multiline_enabled == multiline)
    return;
  m_multiline_enabled = multiline;

  if (m_editline) {
    // Disable edit mode to stop the terminal from flushing all input during
    // the call to el_end() since we expect to have multiple editline instances
    // in this program.
    el_set(m_editline, EL_EDITMODE, 0);
    el_end(m_editline);
  }

  m_editline =
      el_init(m_editor_name.c_str(), m_input_file, m_output_file, m_error_file);
  TerminalSizeChanged();

  if (m_history_sp && m_history_sp->IsValid()) {
    m_history_sp->Load();
    el_wset(m_editline, EL_HIST, history, m_history_sp->GetHistoryPtr());
  }
  el_set(m_editline, EL_CLIENTDATA, this);
  el_set(m_editline, EL_SIGNAL, 0);
  el_set(m_editline, EL_EDITOR, "emacs");
  el_set(m_editline, EL_PROMPT,
         (EditlinePromptCallbackType)([](EditLine *editline) {
           return Editline::InstanceFor(editline)->Prompt();
         }));

  el_wset(m_editline, EL_GETCFN, (EditlineGetCharCallbackType)([](
                                     EditLine *editline, EditLineGetCharType *c) {
            return Editline::InstanceFor(editline)->GetCharacter(c);
          }));

  // Commands used for multiline support, registered whether or not they're
  // used
  el_wset(m_editline, EL_ADDFN, EditLineConstString("lldb-break-line"),
          EditLineConstString("Insert a line break"),
          (EditlineCommandCallbackType)([](EditLine *editline, int ch) {
            return Editline::InstanceFor(editline)->BreakLineCommand(ch);
          }));
  el_wset(m_editline, EL_ADDFN, EditLineConstString("lldb-end-or-add-line"),
          EditLineConstString("End editing or continue when incomplete"),
          (EditlineCommandCallbackType)([](EditLine *editline, int ch) {
            return Editline::InstanceFor(editline)->EndOrAddLineCommand(ch);
          }));
  el_wset(m_editline, EL_ADDFN, EditLineConstString("lldb-delete-next-char"),
          EditLineConstString("Delete next character"),
          (EditlineCommandCallbackType)([](EditLine *editline, int ch) {
            return Editline::InstanceFor(editline)->DeleteNextCharCommand(ch);
          }));
  el_wset(
      m_editline, EL_ADDFN, EditLineConstString("lldb-delete-previous-char"),
      EditLineConstString("Delete previous character"),
      (EditlineCommandCallbackType)([](EditLine *editline, int ch) {
        return Editline::InstanceFor(editline)->DeletePreviousCharCommand(ch);
      }));
  el_wset(m_editline, EL_ADDFN, EditLineConstString("lldb-previous-line"),
          EditLineConstString("Move to previous line"),
          (EditlineCommandCallbackType)([](EditLine *editline, int ch) {
            return Editline::InstanceFor(editline)->PreviousLineCommand(ch);
          }));
  el_wset(m_editline, EL_ADDFN, EditLineConstString("lldb-next-line"),
          EditLineConstString("Move to next line"),
          (EditlineCommandCallbackType)([](EditLine *editline, int ch) {
            return Editline::InstanceFor(editline)->NextLineCommand(ch);
          }));
  el_wset(m_editline, EL_ADDFN, EditLineConstString("lldb-previous-history"),
          EditLineConstString("Move to previous history"),
          (EditlineCommandCallbackType)([](EditLine *editline, int ch) {
            return Editline::InstanceFor(editline)->PreviousHistoryCommand(ch);
          }));
  el_wset(m_editline, EL_ADDFN, EditLineConstString("lldb-next-history"),
          EditLineConstString("Move to next history"),
          (EditlineCommandCallbackType)([](EditLine *editline, int ch) {
            return Editline::InstanceFor(editline)->NextHistoryCommand(ch);
          }));
  el_wset(m_editline, EL_ADDFN, EditLineConstString("lldb-buffer-start"),
          EditLineConstString("Move to start of buffer"),
          (EditlineCommandCallbackType)([](EditLine *editline, int ch) {
            return Editline::InstanceFor(editline)->BufferStartCommand(ch);
          }));
  el_wset(m_editline, EL_ADDFN, EditLineConstString("lldb-buffer-end"),
          EditLineConstString("Move to end of buffer"),
          (EditlineCommandCallbackType)([](EditLine *editline, int ch) {
            return Editline::InstanceFor(editline)->BufferEndCommand(ch);
          }));
  el_wset(m_editline, EL_ADDFN, EditLineConstString("lldb-fix-indentation"),
          EditLineConstString("Fix line indentation"),
          (EditlineCommandCallbackType)([](EditLine *editline, int ch) {
            return Editline::InstanceFor(editline)->FixIndentationCommand(ch);
          }));

  // Register the complete callback under two names for compatibility with
  // older clients using custom .editrc files (largely because libedit has a
  // bad bug where if you have a bind command that tries to bind to a function
  // name that doesn't exist, it can corrupt the heap and crash your process
  // later.)
  EditlineCommandCallbackType complete_callback = [](EditLine *editline,
                                                     int ch) {
    return Editline::InstanceFor(editline)->TabCommand(ch);
  };
  el_wset(m_editline, EL_ADDFN, EditLineConstString("lldb-complete"),
          EditLineConstString("Invoke completion"), complete_callback);
  el_wset(m_editline, EL_ADDFN, EditLineConstString("lldb_complete"),
          EditLineConstString("Invoke completion"), complete_callback);

  // General bindings we don't mind being overridden
  if (!multiline) {
    el_set(m_editline, EL_BIND, "^r", "em-inc-search-prev",
           NULL); // Cycle through backwards search, entering string
  }
  el_set(m_editline, EL_BIND, "^w", "ed-delete-prev-word",
         NULL); // Delete previous word, behave like bash in emacs mode
  el_set(m_editline, EL_BIND, "\t", "lldb-complete",
         NULL); // Bind TAB to auto complete

  // Allow user-specific customization prior to registering bindings we
  // absolutely require
  el_source(m_editline, NULL);

  // Register an internal binding that external developers shouldn't use
  el_wset(m_editline, EL_ADDFN, EditLineConstString("lldb-revert-line"),
          EditLineConstString("Revert line to saved state"),
          (EditlineCommandCallbackType)([](EditLine *editline, int ch) {
            return Editline::InstanceFor(editline)->RevertLineCommand(ch);
          }));

  // Register keys that perform auto-indent correction
  if (m_fix_indentation_callback && m_fix_indentation_callback_chars) {
    char bind_key[2] = {0, 0};
    const char *indent_chars = m_fix_indentation_callback_chars;
    while (*indent_chars) {
      bind_key[0] = *indent_chars;
      el_set(m_editline, EL_BIND, bind_key, "lldb-fix-indentation", NULL);
      ++indent_chars;
    }
  }

  // Multi-line editor bindings
  if (multiline) {
    el_set(m_editline, EL_BIND, "\n", "lldb-end-or-add-line", NULL);
    el_set(m_editline, EL_BIND, "\r", "lldb-end-or-add-line", NULL);
    el_set(m_editline, EL_BIND, ESCAPE "\n", "lldb-break-line", NULL);
    el_set(m_editline, EL_BIND, ESCAPE "\r", "lldb-break-line", NULL);
    el_set(m_editline, EL_BIND, "^p", "lldb-previous-line", NULL);
    el_set(m_editline, EL_BIND, "^n", "lldb-next-line", NULL);
    el_set(m_editline, EL_BIND, "^?", "lldb-delete-previous-char", NULL);
    el_set(m_editline, EL_BIND, "^d", "lldb-delete-next-char", NULL);
    el_set(m_editline, EL_BIND, ESCAPE "[3~", "lldb-delete-next-char", NULL);
    el_set(m_editline, EL_BIND, ESCAPE "[\\^", "lldb-revert-line", NULL);

    // Editor-specific bindings
    if (IsEmacs()) {
      el_set(m_editline, EL_BIND, ESCAPE "<", "lldb-buffer-start", NULL);
      el_set(m_editline, EL_BIND, ESCAPE ">", "lldb-buffer-end", NULL);
      el_set(m_editline, EL_BIND, ESCAPE "[A", "lldb-previous-line", NULL);
      el_set(m_editline, EL_BIND, ESCAPE "[B", "lldb-next-line", NULL);
      el_set(m_editline, EL_BIND, ESCAPE ESCAPE "[A", "lldb-previous-history",
             NULL);
      el_set(m_editline, EL_BIND, ESCAPE ESCAPE "[B", "lldb-next-history",
             NULL);
      el_set(m_editline, EL_BIND, ESCAPE "[1;3A", "lldb-previous-history",
             NULL);
      el_set(m_editline, EL_BIND, ESCAPE "[1;3B", "lldb-next-history", NULL);
    } else {
      el_set(m_editline, EL_BIND, "^H", "lldb-delete-previous-char", NULL);

      el_set(m_editline, EL_BIND, "-a", ESCAPE "[A", "lldb-previous-line",
             NULL);
      el_set(m_editline, EL_BIND, "-a", ESCAPE "[B", "lldb-next-line", NULL);
      el_set(m_editline, EL_BIND, "-a", "x", "lldb-delete-next-char", NULL);
      el_set(m_editline, EL_BIND, "-a", "^H", "lldb-delete-previous-char",
             NULL);
      el_set(m_editline, EL_BIND, "-a", "^?", "lldb-delete-previous-char",
             NULL);

      // Escape is absorbed exiting edit mode, so re-register important
      // sequences without the prefix
      el_set(m_editline, EL_BIND, "-a", "[A", "lldb-previous-line", NULL);
      el_set(m_editline, EL_BIND, "-a", "[B", "lldb-next-line", NULL);
      el_set(m_editline, EL_BIND, "-a", "[\\^", "lldb-revert-line", NULL);
    }
  }
}

//------------------------------------------------------------------
// Editline public methods
//------------------------------------------------------------------

Editline *Editline::InstanceFor(EditLine *editline) {
  Editline *editor;
  el_get(editline, EL_CLIENTDATA, &editor);
  return editor;
}

Editline::Editline(const char *editline_name, FILE *input_file,
                   FILE *output_file, FILE *error_file, bool color_prompts)
    : m_editor_status(EditorStatus::Complete), m_color_prompts(color_prompts),
      m_input_file(input_file), m_output_file(output_file),
      m_error_file(error_file), m_input_connection(fileno(input_file), false) {
  // Get a shared history instance
  m_editor_name = (editline_name == nullptr) ? "lldb-tmp" : editline_name;
  m_history_sp = EditlineHistory::GetHistory(m_editor_name);

#ifdef USE_SETUPTERM_WORKAROUND
  if (m_output_file) {
    const int term_fd = fileno(m_output_file);
    if (term_fd != -1) {
      static std::mutex *g_init_terminal_fds_mutex_ptr = nullptr;
      static std::set<int> *g_init_terminal_fds_ptr = nullptr;
      static llvm::once_flag g_once_flag;
      llvm::call_once(g_once_flag, [&]() {
        g_init_terminal_fds_mutex_ptr =
            new std::mutex(); // NOTE: Leak to avoid C++ destructor chain issues
        g_init_terminal_fds_ptr = new std::set<int>(); // NOTE: Leak to avoid
                                                       // C++ destructor chain
                                                       // issues
      });

      // We must make sure to initialize the terminal a given file descriptor
      // only once. If we do this multiple times, we start leaking memory.
      std::lock_guard<std::mutex> guard(*g_init_terminal_fds_mutex_ptr);
      if (g_init_terminal_fds_ptr->find(term_fd) ==
          g_init_terminal_fds_ptr->end()) {
        g_init_terminal_fds_ptr->insert(term_fd);
        setupterm((char *)0, term_fd, (int *)0);
      }
    }
  }
#endif
}

Editline::~Editline() {
  if (m_editline) {
    // Disable edit mode to stop the terminal from flushing all input during
    // the call to el_end() since we expect to have multiple editline instances
    // in this program.
    el_set(m_editline, EL_EDITMODE, 0);
    el_end(m_editline);
    m_editline = nullptr;
  }

  // EditlineHistory objects are sometimes shared between multiple Editline
  // instances with the same program name. So just release our shared pointer
  // and if we are the last owner, it will save the history to the history save
  // file automatically.
  m_history_sp.reset();
}

void Editline::SetPrompt(const char *prompt) {
  m_set_prompt = prompt == nullptr ? "" : prompt;
}

void Editline::SetContinuationPrompt(const char *continuation_prompt) {
  m_set_continuation_prompt =
      continuation_prompt == nullptr ? "" : continuation_prompt;
}

void Editline::TerminalSizeChanged() {
  if (m_editline != nullptr) {
    el_resize(m_editline);
    int columns;
    // Despite the man page claiming non-zero indicates success, it's actually
    // zero
    if (el_get(m_editline, EL_GETTC, "co", &columns) == 0) {
      m_terminal_width = columns;
      if (m_current_line_rows != -1) {
        const LineInfoW *info = el_wline(m_editline);
        int lineLength =
            (int)((info->lastchar - info->buffer) + GetPromptWidth());
        m_current_line_rows = (lineLength / columns) + 1;
      }
    } else {
      m_terminal_width = INT_MAX;
      m_current_line_rows = 1;
    }
  }
}

const char *Editline::GetPrompt() { return m_set_prompt.c_str(); }

uint32_t Editline::GetCurrentLine() { return m_current_line_index; }

bool Editline::Interrupt() {
  bool result = true;
  std::lock_guard<std::mutex> guard(m_output_mutex);
  if (m_editor_status == EditorStatus::Editing) {
    fprintf(m_output_file, "^C\n");
    result = m_input_connection.InterruptRead();
  }
  m_editor_status = EditorStatus::Interrupted;
  return result;
}

bool Editline::Cancel() {
  bool result = true;
  std::lock_guard<std::mutex> guard(m_output_mutex);
  if (m_editor_status == EditorStatus::Editing) {
    MoveCursor(CursorLocation::EditingCursor, CursorLocation::BlockStart);
    fprintf(m_output_file, ANSI_CLEAR_BELOW);
    result = m_input_connection.InterruptRead();
  }
  m_editor_status = EditorStatus::Interrupted;
  return result;
}

void Editline::SetAutoCompleteCallback(CompleteCallbackType callback,
                                       void *baton) {
  m_completion_callback = callback;
  m_completion_callback_baton = baton;
}

void Editline::SetIsInputCompleteCallback(IsInputCompleteCallbackType callback,
                                          void *baton) {
  m_is_input_complete_callback = callback;
  m_is_input_complete_callback_baton = baton;
}

bool Editline::SetFixIndentationCallback(FixIndentationCallbackType callback,
                                         void *baton,
                                         const char *indent_chars) {
  m_fix_indentation_callback = callback;
  m_fix_indentation_callback_baton = baton;
  m_fix_indentation_callback_chars = indent_chars;
  return false;
}

bool Editline::GetLine(std::string &line, bool &interrupted) {
  ConfigureEditor(false);
  m_input_lines = std::vector<EditLineStringType>();
  m_input_lines.insert(m_input_lines.begin(), EditLineConstString(""));

  std::lock_guard<std::mutex> guard(m_output_mutex);

  lldbassert(m_editor_status != EditorStatus::Editing);
  if (m_editor_status == EditorStatus::Interrupted) {
    m_editor_status = EditorStatus::Complete;
    interrupted = true;
    return true;
  }

  SetCurrentLine(0);
  m_in_history = false;
  m_editor_status = EditorStatus::Editing;
  m_revert_cursor_index = -1;

  int count;
  auto input = el_wgets(m_editline, &count);

  interrupted = m_editor_status == EditorStatus::Interrupted;
  if (!interrupted) {
    if (input == nullptr) {
      fprintf(m_output_file, "\n");
      m_editor_status = EditorStatus::EndOfInput;
    } else {
      m_history_sp->Enter(input);
#if LLDB_EDITLINE_USE_WCHAR
      line = m_utf8conv.to_bytes(SplitLines(input)[0]);
#else
      line = SplitLines(input)[0];
#endif
      m_editor_status = EditorStatus::Complete;
    }
  }
  return m_editor_status != EditorStatus::EndOfInput;
}

bool Editline::GetLines(int first_line_number, StringList &lines,
                        bool &interrupted) {
  ConfigureEditor(true);

  // Print the initial input lines, then move the cursor back up to the start
  // of input
  SetBaseLineNumber(first_line_number);
  m_input_lines = std::vector<EditLineStringType>();
  m_input_lines.insert(m_input_lines.begin(), EditLineConstString(""));

  std::lock_guard<std::mutex> guard(m_output_mutex);
  // Begin the line editing loop
  DisplayInput();
  SetCurrentLine(0);
  MoveCursor(CursorLocation::BlockEnd, CursorLocation::BlockStart);
  m_editor_status = EditorStatus::Editing;
  m_in_history = false;

  m_revert_cursor_index = -1;
  while (m_editor_status == EditorStatus::Editing) {
    int count;
    m_current_line_rows = -1;
    el_wpush(m_editline, EditLineConstString(
                             "\x1b[^")); // Revert to the existing line content
    el_wgets(m_editline, &count);
  }

  interrupted = m_editor_status == EditorStatus::Interrupted;
  if (!interrupted) {
    // Save the completed entry in history before returning
    m_history_sp->Enter(CombineLines(m_input_lines).c_str());

    lines = GetInputAsStringList();
  }
  return m_editor_status != EditorStatus::EndOfInput;
}

void Editline::PrintAsync(Stream *stream, const char *s, size_t len) {
  std::lock_guard<std::mutex> guard(m_output_mutex);
  if (m_editor_status == EditorStatus::Editing) {
    MoveCursor(CursorLocation::EditingCursor, CursorLocation::BlockStart);
    fprintf(m_output_file, ANSI_CLEAR_BELOW);
  }
  stream->Write(s, len);
  stream->Flush();
  if (m_editor_status == EditorStatus::Editing) {
    DisplayInput();
    MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingCursor);
  }
}

bool Editline::CompleteCharacter(char ch, EditLineGetCharType &out) {
#if !LLDB_EDITLINE_USE_WCHAR
  if (ch == (char)EOF)
    return false;

  out = (unsigned char)ch;
  return true;
#else
  std::codecvt_utf8<wchar_t> cvt;
  llvm::SmallString<4> input;
  for (;;) {
    const char *from_next;
    wchar_t *to_next;
    std::mbstate_t state = std::mbstate_t();
    input.push_back(ch);
    switch (cvt.in(state, input.begin(), input.end(), from_next, &out, &out + 1,
                   to_next)) {
    case std::codecvt_base::ok:
      return out != WEOF;

    case std::codecvt_base::error:
    case std::codecvt_base::noconv:
      return false;

    case std::codecvt_base::partial:
      lldb::ConnectionStatus status;
      size_t read_count = m_input_connection.Read(
          &ch, 1, std::chrono::seconds(0), status, nullptr);
      if (read_count == 0)
        return false;
      break;
    }
  }
#endif
}
