//===--- DependencyOutputOptions.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_DEPENDENCYOUTPUTOPTIONS_H
#define LLVM_CLANG_FRONTEND_DEPENDENCYOUTPUTOPTIONS_H

#include <string>
#include <vector>

namespace clang {

/// ShowIncludesDestination - Destination for /showIncludes output.
enum class ShowIncludesDestination { None, Stdout, Stderr };

/// DependencyOutputFormat - Format for the compiler dependency file.
enum class DependencyOutputFormat { Make, NMake };

/// DependencyOutputOptions - Options for controlling the compiler dependency
/// file generation.
class DependencyOutputOptions {
public:
  unsigned IncludeSystemHeaders : 1; ///< Include system header dependencies.
  unsigned ShowHeaderIncludes : 1;   ///< Show header inclusions (-H).
  unsigned UsePhonyTargets : 1;      ///< Include phony targets for each
                                     /// dependency, which can avoid some 'make'
                                     /// problems.
  unsigned AddMissingHeaderDeps : 1; ///< Add missing headers to dependency list
  unsigned IncludeModuleFiles : 1; ///< Include module file dependencies.

  /// Destination of cl.exe style /showIncludes info.
  ShowIncludesDestination ShowIncludesDest = ShowIncludesDestination::None;

  /// The format for the dependency file.
  DependencyOutputFormat OutputFormat = DependencyOutputFormat::Make;

  /// The file to write dependency output to.
  std::string OutputFile;

  /// The file to write header include output to. This is orthogonal to
  /// ShowHeaderIncludes (-H) and will include headers mentioned in the
  /// predefines buffer. If the output file is "-", output will be sent to
  /// stderr.
  std::string HeaderIncludeOutputFile;

  /// A list of names to use as the targets in the dependency file; this list
  /// must contain at least one entry.
  std::vector<std::string> Targets;

  /// A list of filenames to be used as extra dependencies for every target.
  std::vector<std::string> ExtraDeps;

  /// In /showIncludes mode, pretend the main TU is a header with this name.
  std::string ShowIncludesPretendHeader;

  /// The file to write GraphViz-formatted header dependencies to.
  std::string DOTOutputFile;

  /// The directory to copy module dependencies to when collecting them.
  std::string ModuleDependencyOutputDir;

public:
  DependencyOutputOptions()
      : IncludeSystemHeaders(0), ShowHeaderIncludes(0), UsePhonyTargets(0),
        AddMissingHeaderDeps(0), IncludeModuleFiles(0) {}
};

}  // end namespace clang

#endif
