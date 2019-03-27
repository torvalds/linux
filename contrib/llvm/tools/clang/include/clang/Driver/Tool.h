//===--- Tool.h - Compilation Tools -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_DRIVER_TOOL_H
#define LLVM_CLANG_DRIVER_TOOL_H

#include "clang/Basic/LLVM.h"
#include "llvm/Support/Program.h"

namespace llvm {
namespace opt {
  class ArgList;
}
}

namespace clang {
namespace driver {

  class Compilation;
  class InputInfo;
  class Job;
  class JobAction;
  class ToolChain;

  typedef SmallVector<InputInfo, 4> InputInfoList;

/// Tool - Information on a specific compilation tool.
class Tool {
public:
  // Documents the level of support for response files in this tool.
  // Response files are necessary if the command line gets too large,
  // requiring the arguments to be transferred to a file.
  enum ResponseFileSupport {
    // Provides full support for response files, which means we can transfer
    // all tool input arguments to a file. E.g.: clang, gcc, binutils and MSVC
    // tools.
    RF_Full,
    // Input file names can live in a file, but flags can't. E.g.: ld64 (Mac
    // OS X linker).
    RF_FileList,
    // Does not support response files: all arguments must be passed via
    // command line.
    RF_None
  };

private:
  /// The tool name (for debugging).
  const char *Name;

  /// The human readable name for the tool, for use in diagnostics.
  const char *ShortName;

  /// The tool chain this tool is a part of.
  const ToolChain &TheToolChain;

  /// The level of support for response files seen in this tool
  const ResponseFileSupport ResponseSupport;

  /// The encoding to use when writing response files for this tool on Windows
  const llvm::sys::WindowsEncodingMethod ResponseEncoding;

  /// The flag used to pass a response file via command line to this tool
  const char *const ResponseFlag;

public:
  Tool(const char *Name, const char *ShortName, const ToolChain &TC,
       ResponseFileSupport ResponseSupport = RF_None,
       llvm::sys::WindowsEncodingMethod ResponseEncoding = llvm::sys::WEM_UTF8,
       const char *ResponseFlag = "@");

public:
  virtual ~Tool();

  const char *getName() const { return Name; }

  const char *getShortName() const { return ShortName; }

  const ToolChain &getToolChain() const { return TheToolChain; }

  virtual bool hasIntegratedAssembler() const { return false; }
  virtual bool canEmitIR() const { return false; }
  virtual bool hasIntegratedCPP() const = 0;
  virtual bool isLinkJob() const { return false; }
  virtual bool isDsymutilJob() const { return false; }
  /// Returns the level of support for response files of this tool,
  /// whether it accepts arguments to be passed via a file on disk.
  ResponseFileSupport getResponseFilesSupport() const {
    return ResponseSupport;
  }
  /// Returns which encoding the response file should use. This is only
  /// relevant on Windows platforms where there are different encodings being
  /// accepted for different tools. On UNIX, UTF8 is universal.
  ///
  /// Windows use cases: - GCC and Binutils on mingw only accept ANSI response
  /// files encoded with the system current code page.
  /// - MSVC's CL.exe and LINK.exe accept UTF16 on Windows.
  /// - Clang accepts both UTF8 and UTF16.
  ///
  /// FIXME: When GNU tools learn how to parse UTF16 on Windows, we should
  /// always use UTF16 for Windows, which is the Windows official encoding for
  /// international characters.
  llvm::sys::WindowsEncodingMethod getResponseFileEncoding() const {
    return ResponseEncoding;
  }
  /// Returns which prefix to use when passing the name of a response
  /// file as a parameter to this tool.
  const char *getResponseFileFlag() const { return ResponseFlag; }

  /// Does this tool have "good" standardized diagnostics, or should the
  /// driver add an additional "command failed" diagnostic on failures.
  virtual bool hasGoodDiagnostics() const { return false; }

  /// ConstructJob - Construct jobs to perform the action \p JA,
  /// writing to \p Output and with \p Inputs, and add the jobs to
  /// \p C.
  ///
  /// \param TCArgs - The argument list for this toolchain, with any
  /// tool chain specific translations applied.
  /// \param LinkingOutput - If this output will eventually feed the
  /// linker, then this is the final output name of the linked image.
  virtual void ConstructJob(Compilation &C, const JobAction &JA,
                            const InputInfo &Output,
                            const InputInfoList &Inputs,
                            const llvm::opt::ArgList &TCArgs,
                            const char *LinkingOutput) const = 0;
  /// Construct jobs to perform the action \p JA, writing to the \p Outputs and
  /// with \p Inputs, and add the jobs to \p C. The default implementation
  /// assumes a single output and is expected to be overloaded for the tools
  /// that support multiple inputs.
  ///
  /// \param TCArgs The argument list for this toolchain, with any
  /// tool chain specific translations applied.
  /// \param LinkingOutput If this output will eventually feed the
  /// linker, then this is the final output name of the linked image.
  virtual void ConstructJobMultipleOutputs(Compilation &C, const JobAction &JA,
                                           const InputInfoList &Outputs,
                                           const InputInfoList &Inputs,
                                           const llvm::opt::ArgList &TCArgs,
                                           const char *LinkingOutput) const;
};

} // end namespace driver
} // end namespace clang

#endif
