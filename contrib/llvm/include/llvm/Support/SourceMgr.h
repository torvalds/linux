//===- SourceMgr.h - Manager for Source Buffers & Diagnostics ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the SMDiagnostic and SourceMgr classes.  This
// provides a simple substrate for diagnostics, #include handling, and other low
// level things for simple parsers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SOURCEMGR_H
#define LLVM_SUPPORT_SOURCEMGR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SMLoc.h"
#include <algorithm>
#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class raw_ostream;
class SMDiagnostic;
class SMFixIt;

/// This owns the files read by a parser, handles include stacks,
/// and handles diagnostic wrangling.
class SourceMgr {
public:
  enum DiagKind {
    DK_Error,
    DK_Warning,
    DK_Remark,
    DK_Note,
  };

  /// Clients that want to handle their own diagnostics in a custom way can
  /// register a function pointer+context as a diagnostic handler.
  /// It gets called each time PrintMessage is invoked.
  using DiagHandlerTy = void (*)(const SMDiagnostic &, void *Context);

private:
  struct SrcBuffer {
    /// The memory buffer for the file.
    std::unique_ptr<MemoryBuffer> Buffer;

    /// Helper type for OffsetCache below: since we're storing many offsets
    /// into relatively small files (often smaller than 2^8 or 2^16 bytes),
    /// we select the offset vector element type dynamically based on the
    /// size of Buffer.
    using VariableSizeOffsets = PointerUnion4<std::vector<uint8_t> *,
                                              std::vector<uint16_t> *,
                                              std::vector<uint32_t> *,
                                              std::vector<uint64_t> *>;

    /// Vector of offsets into Buffer at which there are line-endings
    /// (lazily populated). Once populated, the '\n' that marks the end of
    /// line number N from [1..] is at Buffer[OffsetCache[N-1]]. Since
    /// these offsets are in sorted (ascending) order, they can be
    /// binary-searched for the first one after any given offset (eg. an
    /// offset corresponding to a particular SMLoc).
    mutable VariableSizeOffsets OffsetCache;

    /// Populate \c OffsetCache and look up a given \p Ptr in it, assuming
    /// it points somewhere into \c Buffer. The static type parameter \p T
    /// must be an unsigned integer type from uint{8,16,32,64}_t large
    /// enough to store offsets inside \c Buffer.
    template<typename T>
    unsigned getLineNumber(const char *Ptr) const;

    /// This is the location of the parent include, or null if at the top level.
    SMLoc IncludeLoc;

    SrcBuffer() = default;
    SrcBuffer(SrcBuffer &&);
    SrcBuffer(const SrcBuffer &) = delete;
    SrcBuffer &operator=(const SrcBuffer &) = delete;
    ~SrcBuffer();
  };

  /// This is all of the buffers that we are reading from.
  std::vector<SrcBuffer> Buffers;

  // This is the list of directories we should search for include files in.
  std::vector<std::string> IncludeDirectories;

  DiagHandlerTy DiagHandler = nullptr;
  void *DiagContext = nullptr;

  bool isValidBufferID(unsigned i) const { return i && i <= Buffers.size(); }

public:
  SourceMgr() = default;
  SourceMgr(const SourceMgr &) = delete;
  SourceMgr &operator=(const SourceMgr &) = delete;
  ~SourceMgr() = default;

  void setIncludeDirs(const std::vector<std::string> &Dirs) {
    IncludeDirectories = Dirs;
  }

  /// Specify a diagnostic handler to be invoked every time PrintMessage is
  /// called. \p Ctx is passed into the handler when it is invoked.
  void setDiagHandler(DiagHandlerTy DH, void *Ctx = nullptr) {
    DiagHandler = DH;
    DiagContext = Ctx;
  }

  DiagHandlerTy getDiagHandler() const { return DiagHandler; }
  void *getDiagContext() const { return DiagContext; }

  const SrcBuffer &getBufferInfo(unsigned i) const {
    assert(isValidBufferID(i));
    return Buffers[i - 1];
  }

  const MemoryBuffer *getMemoryBuffer(unsigned i) const {
    assert(isValidBufferID(i));
    return Buffers[i - 1].Buffer.get();
  }

  unsigned getNumBuffers() const {
    return Buffers.size();
  }

  unsigned getMainFileID() const {
    assert(getNumBuffers());
    return 1;
  }

  SMLoc getParentIncludeLoc(unsigned i) const {
    assert(isValidBufferID(i));
    return Buffers[i - 1].IncludeLoc;
  }

  /// Add a new source buffer to this source manager. This takes ownership of
  /// the memory buffer.
  unsigned AddNewSourceBuffer(std::unique_ptr<MemoryBuffer> F,
                              SMLoc IncludeLoc) {
    SrcBuffer NB;
    NB.Buffer = std::move(F);
    NB.IncludeLoc = IncludeLoc;
    Buffers.push_back(std::move(NB));
    return Buffers.size();
  }

  /// Search for a file with the specified name in the current directory or in
  /// one of the IncludeDirs.
  ///
  /// If no file is found, this returns 0, otherwise it returns the buffer ID
  /// of the stacked file. The full path to the included file can be found in
  /// \p IncludedFile.
  unsigned AddIncludeFile(const std::string &Filename, SMLoc IncludeLoc,
                          std::string &IncludedFile);

  /// Return the ID of the buffer containing the specified location.
  ///
  /// 0 is returned if the buffer is not found.
  unsigned FindBufferContainingLoc(SMLoc Loc) const;

  /// Find the line number for the specified location in the specified file.
  /// This is not a fast method.
  unsigned FindLineNumber(SMLoc Loc, unsigned BufferID = 0) const {
    return getLineAndColumn(Loc, BufferID).first;
  }

  /// Find the line and column number for the specified location in the
  /// specified file. This is not a fast method.
  std::pair<unsigned, unsigned> getLineAndColumn(SMLoc Loc,
                                                 unsigned BufferID = 0) const;

  /// Emit a message about the specified location with the specified string.
  ///
  /// \param ShowColors Display colored messages if output is a terminal and
  /// the default error handler is used.
  void PrintMessage(raw_ostream &OS, SMLoc Loc, DiagKind Kind,
                    const Twine &Msg,
                    ArrayRef<SMRange> Ranges = None,
                    ArrayRef<SMFixIt> FixIts = None,
                    bool ShowColors = true) const;

  /// Emits a diagnostic to llvm::errs().
  void PrintMessage(SMLoc Loc, DiagKind Kind, const Twine &Msg,
                    ArrayRef<SMRange> Ranges = None,
                    ArrayRef<SMFixIt> FixIts = None,
                    bool ShowColors = true) const;

  /// Emits a manually-constructed diagnostic to the given output stream.
  ///
  /// \param ShowColors Display colored messages if output is a terminal and
  /// the default error handler is used.
  void PrintMessage(raw_ostream &OS, const SMDiagnostic &Diagnostic,
                    bool ShowColors = true) const;

  /// Return an SMDiagnostic at the specified location with the specified
  /// string.
  ///
  /// \param Msg If non-null, the kind of message (e.g., "error") which is
  /// prefixed to the message.
  SMDiagnostic GetMessage(SMLoc Loc, DiagKind Kind, const Twine &Msg,
                          ArrayRef<SMRange> Ranges = None,
                          ArrayRef<SMFixIt> FixIts = None) const;

  /// Prints the names of included files and the line of the file they were
  /// included from. A diagnostic handler can use this before printing its
  /// custom formatted message.
  ///
  /// \param IncludeLoc The location of the include.
  /// \param OS the raw_ostream to print on.
  void PrintIncludeStack(SMLoc IncludeLoc, raw_ostream &OS) const;
};

/// Represents a single fixit, a replacement of one range of text with another.
class SMFixIt {
  SMRange Range;

  std::string Text;

public:
  // FIXME: Twine.str() is not very efficient.
  SMFixIt(SMLoc Loc, const Twine &Insertion)
    : Range(Loc, Loc), Text(Insertion.str()) {
    assert(Loc.isValid());
  }

  // FIXME: Twine.str() is not very efficient.
  SMFixIt(SMRange R, const Twine &Replacement)
    : Range(R), Text(Replacement.str()) {
    assert(R.isValid());
  }

  StringRef getText() const { return Text; }
  SMRange getRange() const { return Range; }

  bool operator<(const SMFixIt &Other) const {
    if (Range.Start.getPointer() != Other.Range.Start.getPointer())
      return Range.Start.getPointer() < Other.Range.Start.getPointer();
    if (Range.End.getPointer() != Other.Range.End.getPointer())
      return Range.End.getPointer() < Other.Range.End.getPointer();
    return Text < Other.Text;
  }
};

/// Instances of this class encapsulate one diagnostic report, allowing
/// printing to a raw_ostream as a caret diagnostic.
class SMDiagnostic {
  const SourceMgr *SM = nullptr;
  SMLoc Loc;
  std::string Filename;
  int LineNo = 0;
  int ColumnNo = 0;
  SourceMgr::DiagKind Kind = SourceMgr::DK_Error;
  std::string Message, LineContents;
  std::vector<std::pair<unsigned, unsigned>> Ranges;
  SmallVector<SMFixIt, 4> FixIts;

public:
  // Null diagnostic.
  SMDiagnostic() = default;
  // Diagnostic with no location (e.g. file not found, command line arg error).
  SMDiagnostic(StringRef filename, SourceMgr::DiagKind Knd, StringRef Msg)
    : Filename(filename), LineNo(-1), ColumnNo(-1), Kind(Knd), Message(Msg) {}

  // Diagnostic with a location.
  SMDiagnostic(const SourceMgr &sm, SMLoc L, StringRef FN,
               int Line, int Col, SourceMgr::DiagKind Kind,
               StringRef Msg, StringRef LineStr,
               ArrayRef<std::pair<unsigned,unsigned>> Ranges,
               ArrayRef<SMFixIt> FixIts = None);

  const SourceMgr *getSourceMgr() const { return SM; }
  SMLoc getLoc() const { return Loc; }
  StringRef getFilename() const { return Filename; }
  int getLineNo() const { return LineNo; }
  int getColumnNo() const { return ColumnNo; }
  SourceMgr::DiagKind getKind() const { return Kind; }
  StringRef getMessage() const { return Message; }
  StringRef getLineContents() const { return LineContents; }
  ArrayRef<std::pair<unsigned, unsigned>> getRanges() const { return Ranges; }

  void addFixIt(const SMFixIt &Hint) {
    FixIts.push_back(Hint);
  }

  ArrayRef<SMFixIt> getFixIts() const {
    return FixIts;
  }

  void print(const char *ProgName, raw_ostream &S, bool ShowColors = true,
             bool ShowKindLabel = true) const;
};

} // end namespace llvm

#endif // LLVM_SUPPORT_SOURCEMGR_H
