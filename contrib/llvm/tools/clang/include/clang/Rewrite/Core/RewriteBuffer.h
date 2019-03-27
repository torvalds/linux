//===- RewriteBuffer.h - Buffer rewriting interface -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_REWRITE_CORE_REWRITEBUFFER_H
#define LLVM_CLANG_REWRITE_CORE_REWRITEBUFFER_H

#include "clang/Basic/LLVM.h"
#include "clang/Rewrite/Core/DeltaTree.h"
#include "clang/Rewrite/Core/RewriteRope.h"
#include "llvm/ADT/StringRef.h"

namespace clang {

/// RewriteBuffer - As code is rewritten, SourceBuffer's from the original
/// input with modifications get a new RewriteBuffer associated with them.  The
/// RewriteBuffer captures the modified text itself as well as information used
/// to map between SourceLocation's in the original input and offsets in the
/// RewriteBuffer.  For example, if text is inserted into the buffer, any
/// locations after the insertion point have to be mapped.
class RewriteBuffer {
  friend class Rewriter;

  /// Deltas - Keep track of all the deltas in the source code due to insertions
  /// and deletions.
  DeltaTree Deltas;

  RewriteRope Buffer;

public:
  using iterator = RewriteRope::const_iterator;

  iterator begin() const { return Buffer.begin(); }
  iterator end() const { return Buffer.end(); }
  unsigned size() const { return Buffer.size(); }

  /// Initialize - Start this rewrite buffer out with a copy of the unmodified
  /// input buffer.
  void Initialize(const char *BufStart, const char *BufEnd) {
    Buffer.assign(BufStart, BufEnd);
  }
  void Initialize(StringRef Input) {
    Initialize(Input.begin(), Input.end());
  }

  /// Write to \p Stream the result of applying all changes to the
  /// original buffer.
  /// Note that it isn't safe to use this function to overwrite memory mapped
  /// files in-place (PR17960). Consider using a higher-level utility such as
  /// Rewriter::overwriteChangedFiles() instead.
  ///
  /// The original buffer is not actually changed.
  raw_ostream &write(raw_ostream &Stream) const;

  /// RemoveText - Remove the specified text.
  void RemoveText(unsigned OrigOffset, unsigned Size,
                  bool removeLineIfEmpty = false);

  /// InsertText - Insert some text at the specified point, where the offset in
  /// the buffer is specified relative to the original SourceBuffer.  The
  /// text is inserted after the specified location.
  void InsertText(unsigned OrigOffset, StringRef Str,
                  bool InsertAfter = true);


  /// InsertTextBefore - Insert some text before the specified point, where the
  /// offset in the buffer is specified relative to the original
  /// SourceBuffer. The text is inserted before the specified location.  This is
  /// method is the same as InsertText with "InsertAfter == false".
  void InsertTextBefore(unsigned OrigOffset, StringRef Str) {
    InsertText(OrigOffset, Str, false);
  }

  /// InsertTextAfter - Insert some text at the specified point, where the
  /// offset in the buffer is specified relative to the original SourceBuffer.
  /// The text is inserted after the specified location.
  void InsertTextAfter(unsigned OrigOffset, StringRef Str) {
    InsertText(OrigOffset, Str);
  }

  /// ReplaceText - This method replaces a range of characters in the input
  /// buffer with a new string.  This is effectively a combined "remove/insert"
  /// operation.
  void ReplaceText(unsigned OrigOffset, unsigned OrigLength,
                   StringRef NewStr);

private:
  /// getMappedOffset - Given an offset into the original SourceBuffer that this
  /// RewriteBuffer is based on, map it into the offset space of the
  /// RewriteBuffer.  If AfterInserts is true and if the OrigOffset indicates a
  /// position where text is inserted, the location returned will be after any
  /// inserted text at the position.
  unsigned getMappedOffset(unsigned OrigOffset,
                           bool AfterInserts = false) const{
    return Deltas.getDeltaAt(2*OrigOffset+AfterInserts)+OrigOffset;
  }

  /// AddInsertDelta - When an insertion is made at a position, this
  /// method is used to record that information.
  void AddInsertDelta(unsigned OrigOffset, int Change) {
    return Deltas.AddDelta(2*OrigOffset, Change);
  }

  /// AddReplaceDelta - When a replacement/deletion is made at a position, this
  /// method is used to record that information.
  void AddReplaceDelta(unsigned OrigOffset, int Change) {
    return Deltas.AddDelta(2*OrigOffset+1, Change);
  }
};

} // namespace clang

#endif // LLVM_CLANG_REWRITE_CORE_REWRITEBUFFER_H
