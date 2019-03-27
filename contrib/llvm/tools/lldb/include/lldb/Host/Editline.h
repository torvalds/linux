//===-- Editline.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// TODO: wire up window size changes

// If we ever get a private copy of libedit, there are a number of defects that
// would be nice to fix;
// a) Sometimes text just disappears while editing.  In an 80-column editor
// paste the following text, without
//    the quotes:
//    "This is a test of the input system missing Hello, World!  Do you
//    disappear when it gets to a particular length?"
//    Now press ^A to move to the start and type 3 characters, and you'll see a
//    good amount of the text will
//    disappear.  It's still in the buffer, just invisible.
// b) The prompt printing logic for dealing with ANSI formatting characters is
// broken, which is why we're
//    working around it here.
// c) When resizing the terminal window, if the cursor moves between rows
// libedit will get confused. d) The incremental search uses escape to cancel
// input, so it's confused by
// ANSI sequences starting with escape.
// e) Emoji support is fairly terrible, presumably it doesn't understand
// composed characters?

#ifndef liblldb_Editline_h_
#define liblldb_Editline_h_
#if defined(__cplusplus)

#if LLDB_EDITLINE_USE_WCHAR
#include <codecvt>
#endif
#include <locale>
#include <sstream>
#include <vector>

#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/lldb-private.h"

#if defined(_WIN32)
#include "lldb/Host/windows/editlinewin.h"
#elif !defined(__ANDROID__)
#include <histedit.h>
#endif

#include <mutex>
#include <string>
#include <vector>

#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Predicate.h"

namespace lldb_private {
namespace line_editor {

// type alias's to help manage 8 bit and wide character versions of libedit
#if LLDB_EDITLINE_USE_WCHAR
using EditLineStringType = std::wstring;
using EditLineStringStreamType = std::wstringstream;
using EditLineCharType = wchar_t;
#else
using EditLineStringType = std::string;
using EditLineStringStreamType = std::stringstream;
using EditLineCharType = char;
#endif

// At one point the callback type of el_set getchar callback changed from char
// to wchar_t. It is not possible to detect differentiate between the two
// versions exactly, but this is a pretty good approximation and allows us to
// build against almost any editline version out there.
#if LLDB_EDITLINE_USE_WCHAR || defined(EL_CLIENTDATA) || LLDB_HAVE_EL_RFUNC_T
using EditLineGetCharType = wchar_t;
#else
using EditLineGetCharType = char;
#endif

typedef int (*EditlineGetCharCallbackType)(::EditLine *editline,
                                           EditLineGetCharType *c);
typedef unsigned char (*EditlineCommandCallbackType)(::EditLine *editline,
                                                     int ch);
typedef const char *(*EditlinePromptCallbackType)(::EditLine *editline);

class EditlineHistory;

typedef std::shared_ptr<EditlineHistory> EditlineHistorySP;

typedef bool (*IsInputCompleteCallbackType)(Editline *editline,
                                            StringList &lines, void *baton);

typedef int (*FixIndentationCallbackType)(Editline *editline,
                                          const StringList &lines,
                                          int cursor_position, void *baton);

typedef int (*CompleteCallbackType)(const char *current_line,
                                    const char *cursor, const char *last_char,
                                    int skip_first_n_matches, int max_matches,
                                    StringList &matches,
                                    StringList &descriptions, void *baton);

/// Status used to decide when and how to start editing another line in
/// multi-line sessions
enum class EditorStatus {

  /// The default state proceeds to edit the current line
  Editing,

  /// Editing complete, returns the complete set of edited lines
  Complete,

  /// End of input reported
  EndOfInput,

  /// Editing interrupted
  Interrupted
};

/// Established locations that can be easily moved among with MoveCursor
enum class CursorLocation {
  /// The start of the first line in a multi-line edit session
  BlockStart,

  /// The start of the current line in a multi-line edit session
  EditingPrompt,

  /// The location of the cursor on the current line in a multi-line edit
  /// session
  EditingCursor,

  /// The location immediately after the last character in a multi-line edit
  /// session
  BlockEnd
};
}

using namespace line_editor;

/// Instances of Editline provide an abstraction over libedit's EditLine
/// facility.  Both
/// single- and multi-line editing are supported.
class Editline {
public:
  Editline(const char *editor_name, FILE *input_file, FILE *output_file,
           FILE *error_file, bool color_prompts);

  ~Editline();

  /// Uses the user data storage of EditLine to retrieve an associated instance
  /// of Editline.
  static Editline *InstanceFor(::EditLine *editline);

  /// Sets a string to be used as a prompt, or combined with a line number to
  /// form a prompt.
  void SetPrompt(const char *prompt);

  /// Sets an alternate string to be used as a prompt for the second line and
  /// beyond in multi-line
  /// editing scenarios.
  void SetContinuationPrompt(const char *continuation_prompt);

  /// Required to update the width of the terminal registered for I/O.  It is
  /// critical that this
  /// be correct at all times.
  void TerminalSizeChanged();

  /// Returns the prompt established by SetPrompt()
  const char *GetPrompt();

  /// Returns the index of the line currently being edited
  uint32_t GetCurrentLine();

  /// Interrupt the current edit as if ^C was pressed
  bool Interrupt();

  /// Cancel this edit and oblitarate all trace of it
  bool Cancel();

  /// Register a callback for the tab key
  void SetAutoCompleteCallback(CompleteCallbackType callback, void *baton);

  /// Register a callback for testing whether multi-line input is complete
  void SetIsInputCompleteCallback(IsInputCompleteCallbackType callback,
                                  void *baton);

  /// Register a callback for determining the appropriate indentation for a line
  /// when creating a newline.  An optional set of insertable characters can
  /// also
  /// trigger the callback.
  bool SetFixIndentationCallback(FixIndentationCallbackType callback,
                                 void *baton, const char *indent_chars);

  /// Prompts for and reads a single line of user input.
  bool GetLine(std::string &line, bool &interrupted);

  /// Prompts for and reads a multi-line batch of user input.
  bool GetLines(int first_line_number, StringList &lines, bool &interrupted);

  void PrintAsync(Stream *stream, const char *s, size_t len);

private:
  /// Sets the lowest line number for multi-line editing sessions.  A value of
  /// zero suppresses
  /// line number printing in the prompt.
  void SetBaseLineNumber(int line_number);

  /// Returns the complete prompt by combining the prompt or continuation prompt
  /// with line numbers
  /// as appropriate.  The line index is a zero-based index into the current
  /// multi-line session.
  std::string PromptForIndex(int line_index);

  /// Sets the current line index between line edits to allow free movement
  /// between lines.  Updates
  /// the prompt to match.
  void SetCurrentLine(int line_index);

  /// Determines the width of the prompt in characters.  The width is guaranteed
  /// to be the same for
  /// all lines of the current multi-line session.
  int GetPromptWidth();

  /// Returns true if the underlying EditLine session's keybindings are
  /// Emacs-based, or false if
  /// they are VI-based.
  bool IsEmacs();

  /// Returns true if the current EditLine buffer contains nothing but spaces,
  /// or is empty.
  bool IsOnlySpaces();

  /// Helper method used by MoveCursor to determine relative line position.
  int GetLineIndexForLocation(CursorLocation location, int cursor_row);

  /// Move the cursor from one well-established location to another using
  /// relative line positioning
  /// and absolute column positioning.
  void MoveCursor(CursorLocation from, CursorLocation to);

  /// Clear from cursor position to bottom of screen and print input lines
  /// including prompts, optionally
  /// starting from a specific line.  Lines are drawn with an extra space at the
  /// end to reserve room for
  /// the rightmost cursor position.
  void DisplayInput(int firstIndex = 0);

  /// Counts the number of rows a given line of content will end up occupying,
  /// taking into account both
  /// the preceding prompt and a single trailing space occupied by a cursor when
  /// at the end of the line.
  int CountRowsForLine(const EditLineStringType &content);

  /// Save the line currently being edited
  void SaveEditedLine();

  /// Convert the current input lines into a UTF8 StringList
  StringList GetInputAsStringList(int line_count = UINT32_MAX);

  /// Replaces the current multi-line session with the next entry from history.
  /// When the parameter is
  /// true it will take the next earlier entry from history, when it is false it
  /// takes the next most
  /// recent.
  unsigned char RecallHistory(bool earlier);

  /// Character reading implementation for EditLine that supports our multi-line
  /// editing trickery.
  int GetCharacter(EditLineGetCharType *c);

  /// Prompt implementation for EditLine.
  const char *Prompt();

  /// Line break command used when meta+return is pressed in multi-line mode.
  unsigned char BreakLineCommand(int ch);

  /// Command used when return is pressed in multi-line mode.
  unsigned char EndOrAddLineCommand(int ch);

  /// Delete command used when delete is pressed in multi-line mode.
  unsigned char DeleteNextCharCommand(int ch);

  /// Delete command used when backspace is pressed in multi-line mode.
  unsigned char DeletePreviousCharCommand(int ch);

  /// Line navigation command used when ^P or up arrow are pressed in multi-line
  /// mode.
  unsigned char PreviousLineCommand(int ch);

  /// Line navigation command used when ^N or down arrow are pressed in
  /// multi-line mode.
  unsigned char NextLineCommand(int ch);

  /// History navigation command used when Alt + up arrow is pressed in
  /// multi-line mode.
  unsigned char PreviousHistoryCommand(int ch);

  /// History navigation command used when Alt + down arrow is pressed in
  /// multi-line mode.
  unsigned char NextHistoryCommand(int ch);

  /// Buffer start command used when Esc < is typed in multi-line emacs mode.
  unsigned char BufferStartCommand(int ch);

  /// Buffer end command used when Esc > is typed in multi-line emacs mode.
  unsigned char BufferEndCommand(int ch);

  /// Context-sensitive tab insertion or code completion command used when the
  /// tab key is typed.
  unsigned char TabCommand(int ch);

  /// Respond to normal character insertion by fixing line indentation
  unsigned char FixIndentationCommand(int ch);

  /// Revert line command used when moving between lines.
  unsigned char RevertLineCommand(int ch);

  /// Ensures that the current EditLine instance is properly configured for
  /// single or multi-line editing.
  void ConfigureEditor(bool multiline);

  bool CompleteCharacter(char ch, EditLineGetCharType &out);

private:
#if LLDB_EDITLINE_USE_WCHAR
  std::wstring_convert<std::codecvt_utf8<wchar_t>> m_utf8conv;
#endif
  ::EditLine *m_editline = nullptr;
  EditlineHistorySP m_history_sp;
  bool m_in_history = false;
  std::vector<EditLineStringType> m_live_history_lines;
  bool m_multiline_enabled = false;
  std::vector<EditLineStringType> m_input_lines;
  EditorStatus m_editor_status;
  bool m_color_prompts = true;
  int m_terminal_width = 0;
  int m_base_line_number = 0;
  unsigned m_current_line_index = 0;
  int m_current_line_rows = -1;
  int m_revert_cursor_index = 0;
  int m_line_number_digits = 3;
  std::string m_set_prompt;
  std::string m_set_continuation_prompt;
  std::string m_current_prompt;
  bool m_needs_prompt_repaint = false;
  std::string m_editor_name;
  FILE *m_input_file;
  FILE *m_output_file;
  FILE *m_error_file;
  ConnectionFileDescriptor m_input_connection;
  IsInputCompleteCallbackType m_is_input_complete_callback = nullptr;
  void *m_is_input_complete_callback_baton = nullptr;
  FixIndentationCallbackType m_fix_indentation_callback = nullptr;
  void *m_fix_indentation_callback_baton = nullptr;
  const char *m_fix_indentation_callback_chars = nullptr;
  CompleteCallbackType m_completion_callback = nullptr;
  void *m_completion_callback_baton = nullptr;

  std::mutex m_output_mutex;
};
}

#endif // #if defined(__cplusplus)
#endif // liblldb_Editline_h_
