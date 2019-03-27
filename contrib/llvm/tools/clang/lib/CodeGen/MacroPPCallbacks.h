//===--- MacroPPCallbacks.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines implementation for the macro preprocessors callbacks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_MACROPPCALLBACKS_H
#define LLVM_CLANG_LIB_CODEGEN_MACROPPCALLBACKS_H

#include "clang/Lex/PPCallbacks.h"

namespace llvm {
class DIMacroFile;
class DIMacroNode;
}
namespace clang {
class Preprocessor;
class MacroInfo;
class CodeGenerator;

class MacroPPCallbacks : public PPCallbacks {
  /// A pointer to code generator, where debug info generator can be found.
  CodeGenerator *Gen;

  /// Preprocessor.
  Preprocessor &PP;

  /// Location of recent included file, used for line number.
  SourceLocation LastHashLoc;

  /// Counts current number of command line included files, which were entered
  /// and were not exited yet.
  int EnteredCommandLineIncludeFiles = 0;

  enum FileScopeStatus {
    NoScope = 0,              // Scope is not initialized yet.
    InitializedScope,         // Main file scope is initialized but not set yet.
    BuiltinScope,             // <built-in> and <command line> file scopes.
    CommandLineIncludeScope,  // Included file, from <command line> file, scope.
    MainFileScope             // Main file scope.
  };
  FileScopeStatus Status;

  /// Parent contains all entered files that were not exited yet according to
  /// the inclusion order.
  llvm::SmallVector<llvm::DIMacroFile *, 4> Scopes;

  /// Get current DIMacroFile scope.
  /// \return current DIMacroFile scope or nullptr if there is no such scope.
  llvm::DIMacroFile *getCurrentScope();

  /// Get current line location or invalid location.
  /// \param Loc current line location.
  /// \return current line location \p `Loc`, or invalid location if it's in a
  ///         skipped file scope.
  SourceLocation getCorrectLocation(SourceLocation Loc);

  /// Use the passed preprocessor to write the macro name and value from the
  /// given macro info and identifier info into the given \p `Name` and \p
  /// `Value` output streams.
  ///
  /// \param II Identifier info, used to get the Macro name.
  /// \param MI Macro info, used to get the Macro argumets and values.
  /// \param PP Preprocessor.
  /// \param [out] Name Place holder for returned macro name and arguments.
  /// \param [out] Value Place holder for returned macro value.
  static void writeMacroDefinition(const IdentifierInfo &II,
                                   const MacroInfo &MI, Preprocessor &PP,
                                   raw_ostream &Name, raw_ostream &Value);

  /// Update current file scope status to next file scope.
  void updateStatusToNextScope();

  /// Handle the case when entering a file.
  ///
  /// \param Loc Indicates the new location.
  void FileEntered(SourceLocation Loc);

  /// Handle the case when exiting a file.
  ///
  /// \param Loc Indicates the new location.
  void FileExited(SourceLocation Loc);

public:
  MacroPPCallbacks(CodeGenerator *Gen, Preprocessor &PP);

  /// Callback invoked whenever a source file is entered or exited.
  ///
  /// \param Loc Indicates the new location.
  /// \param PrevFID the file that was exited if \p Reason is ExitFile.
  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                   SrcMgr::CharacteristicKind FileType,
                   FileID PrevFID = FileID()) override;

  /// Callback invoked whenever a directive (#xxx) is processed.
  void InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                          StringRef FileName, bool IsAngled,
                          CharSourceRange FilenameRange, const FileEntry *File,
                          StringRef SearchPath, StringRef RelativePath,
                          const Module *Imported,
                          SrcMgr::CharacteristicKind FileType) override;

  /// Hook called whenever a macro definition is seen.
  void MacroDefined(const Token &MacroNameTok,
                    const MacroDirective *MD) override;

  /// Hook called whenever a macro \#undef is seen.
  ///
  /// MD is released immediately following this callback.
  void MacroUndefined(const Token &MacroNameTok, const MacroDefinition &MD,
                      const MacroDirective *Undef) override;
};

} // end namespace clang

#endif
