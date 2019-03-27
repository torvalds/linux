//===-- llvm/LineEditor/LineEditor.h - line editor --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LINEEDITOR_LINEEDITOR_H
#define LLVM_LINEEDITOR_LINEEDITOR_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class LineEditor {
public:
  /// Create a LineEditor object.
  ///
  /// \param ProgName The name of the current program. Used to form a default
  /// prompt.
  /// \param HistoryPath Path to the file in which to store history data, if
  /// possible.
  /// \param In The input stream used by the editor.
  /// \param Out The output stream used by the editor.
  /// \param Err The error stream used by the editor.
  LineEditor(StringRef ProgName, StringRef HistoryPath = "", FILE *In = stdin,
             FILE *Out = stdout, FILE *Err = stderr);
  ~LineEditor();

  /// Reads a line.
  ///
  /// \return The line, or llvm::Optional<std::string>() on EOF.
  llvm::Optional<std::string> readLine() const;

  void saveHistory();
  void loadHistory();

  static std::string getDefaultHistoryPath(StringRef ProgName);

  /// The action to perform upon a completion request.
  struct CompletionAction {
    enum ActionKind {
      /// Insert Text at the cursor position.
      AK_Insert,
      /// Show Completions, or beep if the list is empty.
      AK_ShowCompletions
    };

    ActionKind Kind;

    /// The text to insert.
    std::string Text;

    /// The list of completions to show.
    std::vector<std::string> Completions;
  };

  /// A possible completion at a given cursor position.
  struct Completion {
    Completion() {}
    Completion(const std::string &TypedText, const std::string &DisplayText)
        : TypedText(TypedText), DisplayText(DisplayText) {}

    /// The text to insert. If the user has already input some of the
    /// completion, this should only include the rest of the text.
    std::string TypedText;

    /// A description of this completion. This may be the completion itself, or
    /// maybe a summary of its type or arguments.
    std::string DisplayText;
  };

  /// Set the completer for this LineEditor. A completer is a function object
  /// which takes arguments of type StringRef (the string to complete) and
  /// size_t (the zero-based cursor position in the StringRef) and returns a
  /// CompletionAction.
  template <typename T> void setCompleter(T Comp) {
    Completer.reset(new CompleterModel<T>(Comp));
  }

  /// Set the completer for this LineEditor to the given list completer.
  /// A list completer is a function object which takes arguments of type
  /// StringRef (the string to complete) and size_t (the zero-based cursor
  /// position in the StringRef) and returns a std::vector<Completion>.
  template <typename T> void setListCompleter(T Comp) {
    Completer.reset(new ListCompleterModel<T>(Comp));
  }

  /// Use the current completer to produce a CompletionAction for the given
  /// completion request. If the current completer is a list completer, this
  /// will return an AK_Insert CompletionAction if each completion has a common
  /// prefix, or an AK_ShowCompletions CompletionAction otherwise.
  ///
  /// \param Buffer The string to complete
  /// \param Pos The zero-based cursor position in the StringRef
  CompletionAction getCompletionAction(StringRef Buffer, size_t Pos) const;

  const std::string &getPrompt() const { return Prompt; }
  void setPrompt(const std::string &P) { Prompt = P; }

  // Public so callbacks in LineEditor.cpp can use it.
  struct InternalData;

private:
  std::string Prompt;
  std::string HistoryPath;
  std::unique_ptr<InternalData> Data;

  struct CompleterConcept {
    virtual ~CompleterConcept();
    virtual CompletionAction complete(StringRef Buffer, size_t Pos) const = 0;
  };

  struct ListCompleterConcept : CompleterConcept {
    ~ListCompleterConcept() override;
    CompletionAction complete(StringRef Buffer, size_t Pos) const override;
    static std::string getCommonPrefix(const std::vector<Completion> &Comps);
    virtual std::vector<Completion> getCompletions(StringRef Buffer,
                                                   size_t Pos) const = 0;
  };

  template <typename T>
  struct CompleterModel : CompleterConcept {
    CompleterModel(T Value) : Value(Value) {}
    CompletionAction complete(StringRef Buffer, size_t Pos) const override {
      return Value(Buffer, Pos);
    }
    T Value;
  };

  template <typename T>
  struct ListCompleterModel : ListCompleterConcept {
    ListCompleterModel(T Value) : Value(std::move(Value)) {}
    std::vector<Completion> getCompletions(StringRef Buffer,
                                           size_t Pos) const override {
      return Value(Buffer, Pos);
    }
    T Value;
  };

  std::unique_ptr<const CompleterConcept> Completer;
};

}

#endif
