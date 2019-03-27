//===-- CompletionRequest.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_COMPLETIONREQUEST_H
#define LLDB_UTILITY_COMPLETIONREQUEST_H

#include "lldb/Utility/Args.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/StringList.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"

namespace lldb_private {
class CompletionResult {
  //----------------------------------------------------------
  /// A single completion and all associated data.
  //----------------------------------------------------------
  struct Completion {
    Completion(llvm::StringRef completion, llvm::StringRef description)
        : m_completion(completion.str()), m_descripton(description.str()) {}

    std::string m_completion;
    std::string m_descripton;

    /// Generates a string that uniquely identifies this completion result.
    std::string GetUniqueKey() const;
  };
  std::vector<Completion> m_results;

  /// List of added completions so far. Used to filter out duplicates.
  llvm::StringSet<> m_added_values;

public:
  void AddResult(llvm::StringRef completion, llvm::StringRef description);

  //----------------------------------------------------------
  /// Adds all collected completion matches to the given list.
  /// The list will be cleared before the results are added. The number of
  /// results here is guaranteed to be equal to GetNumberOfResults().
  //----------------------------------------------------------
  void GetMatches(StringList &matches) const;

  //----------------------------------------------------------
  /// Adds all collected completion descriptions to the given list.
  /// The list will be cleared before the results are added. The number of
  /// results here is guaranteed to be equal to GetNumberOfResults().
  //----------------------------------------------------------
  void GetDescriptions(StringList &descriptions) const;

  std::size_t GetNumberOfResults() const { return m_results.size(); }
};

//----------------------------------------------------------------------
/// @class CompletionRequest CompletionRequest.h
///   "lldb/Utility/ArgCompletionRequest.h"
///
/// Contains all information necessary to complete an incomplete command
/// for the user. Will be filled with the generated completions by the different
/// completions functions.
///
//----------------------------------------------------------------------
class CompletionRequest {
public:
  //----------------------------------------------------------
  /// Constructs a completion request.
  ///
  /// @param [in] command_line
  ///     The command line the user has typed at this point.
  ///
  /// @param [in] raw_cursor_pos
  ///     The position of the cursor in the command line string. Index 0 means
  ///     the cursor is at the start of the line. The completion starts from
  ///     this cursor position.
  ///
  /// @param [in] match_start_point
  /// @param [in] max_return_elements
  ///     If there is a match that is expensive to compute, these are here to
  ///     allow you to compute the completions in  batches.  Start the
  ///     completion from match_start_point, and return match_return_elements
  ///     elements.
  ///
  /// @param [out] result
  ///     The CompletionResult that will be filled with the results after this
  ///     request has been handled.
  //----------------------------------------------------------
  CompletionRequest(llvm::StringRef command_line, unsigned raw_cursor_pos,
                    int match_start_point, int max_return_elements,
                    CompletionResult &result);

  llvm::StringRef GetRawLine() const { return m_command; }

  unsigned GetRawCursorPos() const { return m_raw_cursor_pos; }

  const Args &GetParsedLine() const { return m_parsed_line; }

  Args &GetParsedLine() { return m_parsed_line; }

  const Args &GetPartialParsedLine() const { return m_partial_parsed_line; }

  void SetCursorIndex(int i) { m_cursor_index = i; }
  int GetCursorIndex() const { return m_cursor_index; }

  void SetCursorCharPosition(int pos) { m_cursor_char_position = pos; }
  int GetCursorCharPosition() const { return m_cursor_char_position; }

  int GetMatchStartPoint() const { return m_match_start_point; }

  int GetMaxReturnElements() const { return m_max_return_elements; }

  bool GetWordComplete() { return m_word_complete; }

  void SetWordComplete(bool v) { m_word_complete = v; }

  /// Adds a possible completion string. If the completion was already
  /// suggested before, it will not be added to the list of results. A copy of
  /// the suggested completion is stored, so the given string can be free'd
  /// afterwards.
  ///
  /// @param match The suggested completion.
  /// @param match An optional description of the completion string. The
  ///     description will be displayed to the user alongside the completion.
  void AddCompletion(llvm::StringRef completion,
                     llvm::StringRef description = "") {
    m_result.AddResult(completion, description);
  }

  /// Adds multiple possible completion strings.
  ///
  /// \param completions The list of completions.
  ///
  /// @see AddCompletion
  void AddCompletions(const StringList &completions) {
    for (std::size_t i = 0; i < completions.GetSize(); ++i)
      AddCompletion(completions.GetStringAtIndex(i));
  }

  /// Adds multiple possible completion strings alongside their descriptions.
  ///
  /// The number of completions and descriptions must be identical.
  ///
  /// \param completions The list of completions.
  /// \param completions The list of descriptions.
  ///
  /// @see AddCompletion
  void AddCompletions(const StringList &completions,
                      const StringList &descriptions) {
    lldbassert(completions.GetSize() == descriptions.GetSize());
    for (std::size_t i = 0; i < completions.GetSize(); ++i)
      AddCompletion(completions.GetStringAtIndex(i),
                    descriptions.GetStringAtIndex(i));
  }

  std::size_t GetNumberOfMatches() const {
    return m_result.GetNumberOfResults();
  }

  llvm::StringRef GetCursorArgument() const {
    return GetParsedLine().GetArgumentAtIndex(GetCursorIndex());
  }

  llvm::StringRef GetCursorArgumentPrefix() const {
    return GetCursorArgument().substr(0, GetCursorCharPosition());
  }

private:
  /// The raw command line we are supposed to complete.
  llvm::StringRef m_command;
  /// The cursor position in m_command.
  unsigned m_raw_cursor_pos;
  /// The command line parsed as arguments.
  Args m_parsed_line;
  /// The command line until the cursor position parsed as arguments.
  Args m_partial_parsed_line;
  /// The index of the argument in which the completion cursor is.
  int m_cursor_index;
  /// The cursor position in the argument indexed by m_cursor_index.
  int m_cursor_char_position;
  /// If there is a match that is expensive
  /// to compute, these are here to allow you to compute the completions in
  /// batches.  Start the completion from \amatch_start_point, and return
  /// \amatch_return_elements elements.
  // FIXME: These two values are not implemented.
  int m_match_start_point;
  int m_max_return_elements;
  /// \btrue if this is a complete option value (a space will be inserted
  /// after the completion.)  \bfalse otherwise.
  bool m_word_complete = false;

  /// The result this request is supposed to fill out.
  /// We keep this object private to ensure that no backend can in any way
  /// depend on already calculated completions (which would make debugging and
  /// testing them much more complicated).
  CompletionResult &m_result;
};

} // namespace lldb_private

#endif // LLDB_UTILITY_COMPLETIONREQUEST_H
