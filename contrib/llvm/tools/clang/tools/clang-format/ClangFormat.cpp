//===-- clang-format/ClangFormat.cpp - Clang format tool ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a clang-format tool that automatically formats
/// (fragments of) C++ code.
///
//===----------------------------------------------------------------------===//

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Version.h"
#include "clang/Format/Format.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Process.h"

using namespace llvm;
using clang::tooling::Replacements;

static cl::opt<bool> Help("h", cl::desc("Alias for -help"), cl::Hidden);

// Mark all our options with this category, everything else (except for -version
// and -help) will be hidden.
static cl::OptionCategory ClangFormatCategory("Clang-format options");

static cl::list<unsigned>
    Offsets("offset",
            cl::desc("Format a range starting at this byte offset.\n"
                     "Multiple ranges can be formatted by specifying\n"
                     "several -offset and -length pairs.\n"
                     "Can only be used with one input file."),
            cl::cat(ClangFormatCategory));
static cl::list<unsigned>
    Lengths("length",
            cl::desc("Format a range of this length (in bytes).\n"
                     "Multiple ranges can be formatted by specifying\n"
                     "several -offset and -length pairs.\n"
                     "When only a single -offset is specified without\n"
                     "-length, clang-format will format up to the end\n"
                     "of the file.\n"
                     "Can only be used with one input file."),
            cl::cat(ClangFormatCategory));
static cl::list<std::string>
LineRanges("lines", cl::desc("<start line>:<end line> - format a range of\n"
                             "lines (both 1-based).\n"
                             "Multiple ranges can be formatted by specifying\n"
                             "several -lines arguments.\n"
                             "Can't be used with -offset and -length.\n"
                             "Can only be used with one input file."),
           cl::cat(ClangFormatCategory));
static cl::opt<std::string>
    Style("style", cl::desc(clang::format::StyleOptionHelpDescription),
          cl::init(clang::format::DefaultFormatStyle),
          cl::cat(ClangFormatCategory));
static cl::opt<std::string>
    FallbackStyle("fallback-style",
                  cl::desc("The name of the predefined style used as a\n"
                           "fallback in case clang-format is invoked with\n"
                           "-style=file, but can not find the .clang-format\n"
                           "file to use.\n"
                           "Use -fallback-style=none to skip formatting."),
                  cl::init(clang::format::DefaultFallbackStyle),
                  cl::cat(ClangFormatCategory));

static cl::opt<std::string>
AssumeFileName("assume-filename",
               cl::desc("When reading from stdin, clang-format assumes this\n"
                        "filename to look for a style config file (with\n"
                        "-style=file) and to determine the language."),
               cl::init("<stdin>"), cl::cat(ClangFormatCategory));

static cl::opt<bool> Inplace("i",
                             cl::desc("Inplace edit <file>s, if specified."),
                             cl::cat(ClangFormatCategory));

static cl::opt<bool> OutputXML("output-replacements-xml",
                               cl::desc("Output replacements as XML."),
                               cl::cat(ClangFormatCategory));
static cl::opt<bool>
    DumpConfig("dump-config",
               cl::desc("Dump configuration options to stdout and exit.\n"
                        "Can be used with -style option."),
               cl::cat(ClangFormatCategory));
static cl::opt<unsigned>
    Cursor("cursor",
           cl::desc("The position of the cursor when invoking\n"
                    "clang-format from an editor integration"),
           cl::init(0), cl::cat(ClangFormatCategory));

static cl::opt<bool> SortIncludes(
    "sort-includes",
    cl::desc("If set, overrides the include sorting behavior determined by the "
             "SortIncludes style flag"),
    cl::cat(ClangFormatCategory));

static cl::opt<bool>
    Verbose("verbose", cl::desc("If set, shows the list of processed files"),
            cl::cat(ClangFormatCategory));

static cl::list<std::string> FileNames(cl::Positional, cl::desc("[<file> ...]"),
                                       cl::cat(ClangFormatCategory));

namespace clang {
namespace format {

static FileID createInMemoryFile(StringRef FileName, MemoryBuffer *Source,
                                 SourceManager &Sources, FileManager &Files,
                                 llvm::vfs::InMemoryFileSystem *MemFS) {
  MemFS->addFileNoOwn(FileName, 0, Source);
  return Sources.createFileID(Files.getFile(FileName), SourceLocation(),
                              SrcMgr::C_User);
}

// Parses <start line>:<end line> input to a pair of line numbers.
// Returns true on error.
static bool parseLineRange(StringRef Input, unsigned &FromLine,
                           unsigned &ToLine) {
  std::pair<StringRef, StringRef> LineRange = Input.split(':');
  return LineRange.first.getAsInteger(0, FromLine) ||
         LineRange.second.getAsInteger(0, ToLine);
}

static bool fillRanges(MemoryBuffer *Code,
                       std::vector<tooling::Range> &Ranges) {
  IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> InMemoryFileSystem(
      new llvm::vfs::InMemoryFileSystem);
  FileManager Files(FileSystemOptions(), InMemoryFileSystem);
  DiagnosticsEngine Diagnostics(
      IntrusiveRefCntPtr<DiagnosticIDs>(new DiagnosticIDs),
      new DiagnosticOptions);
  SourceManager Sources(Diagnostics, Files);
  FileID ID = createInMemoryFile("<irrelevant>", Code, Sources, Files,
                                 InMemoryFileSystem.get());
  if (!LineRanges.empty()) {
    if (!Offsets.empty() || !Lengths.empty()) {
      errs() << "error: cannot use -lines with -offset/-length\n";
      return true;
    }

    for (unsigned i = 0, e = LineRanges.size(); i < e; ++i) {
      unsigned FromLine, ToLine;
      if (parseLineRange(LineRanges[i], FromLine, ToLine)) {
        errs() << "error: invalid <start line>:<end line> pair\n";
        return true;
      }
      if (FromLine > ToLine) {
        errs() << "error: start line should be less than end line\n";
        return true;
      }
      SourceLocation Start = Sources.translateLineCol(ID, FromLine, 1);
      SourceLocation End = Sources.translateLineCol(ID, ToLine, UINT_MAX);
      if (Start.isInvalid() || End.isInvalid())
        return true;
      unsigned Offset = Sources.getFileOffset(Start);
      unsigned Length = Sources.getFileOffset(End) - Offset;
      Ranges.push_back(tooling::Range(Offset, Length));
    }
    return false;
  }

  if (Offsets.empty())
    Offsets.push_back(0);
  if (Offsets.size() != Lengths.size() &&
      !(Offsets.size() == 1 && Lengths.empty())) {
    errs() << "error: number of -offset and -length arguments must match.\n";
    return true;
  }
  for (unsigned i = 0, e = Offsets.size(); i != e; ++i) {
    if (Offsets[i] >= Code->getBufferSize()) {
      errs() << "error: offset " << Offsets[i] << " is outside the file\n";
      return true;
    }
    SourceLocation Start =
        Sources.getLocForStartOfFile(ID).getLocWithOffset(Offsets[i]);
    SourceLocation End;
    if (i < Lengths.size()) {
      if (Offsets[i] + Lengths[i] > Code->getBufferSize()) {
        errs() << "error: invalid length " << Lengths[i]
               << ", offset + length (" << Offsets[i] + Lengths[i]
               << ") is outside the file.\n";
        return true;
      }
      End = Start.getLocWithOffset(Lengths[i]);
    } else {
      End = Sources.getLocForEndOfFile(ID);
    }
    unsigned Offset = Sources.getFileOffset(Start);
    unsigned Length = Sources.getFileOffset(End) - Offset;
    Ranges.push_back(tooling::Range(Offset, Length));
  }
  return false;
}

static void outputReplacementXML(StringRef Text) {
  // FIXME: When we sort includes, we need to make sure the stream is correct
  // utf-8.
  size_t From = 0;
  size_t Index;
  while ((Index = Text.find_first_of("\n\r<&", From)) != StringRef::npos) {
    outs() << Text.substr(From, Index - From);
    switch (Text[Index]) {
    case '\n':
      outs() << "&#10;";
      break;
    case '\r':
      outs() << "&#13;";
      break;
    case '<':
      outs() << "&lt;";
      break;
    case '&':
      outs() << "&amp;";
      break;
    default:
      llvm_unreachable("Unexpected character encountered!");
    }
    From = Index + 1;
  }
  outs() << Text.substr(From);
}

static void outputReplacementsXML(const Replacements &Replaces) {
  for (const auto &R : Replaces) {
    outs() << "<replacement "
           << "offset='" << R.getOffset() << "' "
           << "length='" << R.getLength() << "'>";
    outputReplacementXML(R.getReplacementText());
    outs() << "</replacement>\n";
  }
}

// Returns true on error.
static bool format(StringRef FileName) {
  if (!OutputXML && Inplace && FileName == "-") {
    errs() << "error: cannot use -i when reading from stdin.\n";
    return false;
  }
  // On Windows, overwriting a file with an open file mapping doesn't work,
  // so read the whole file into memory when formatting in-place.
  ErrorOr<std::unique_ptr<MemoryBuffer>> CodeOrErr =
      !OutputXML && Inplace ? MemoryBuffer::getFileAsStream(FileName) :
                              MemoryBuffer::getFileOrSTDIN(FileName);
  if (std::error_code EC = CodeOrErr.getError()) {
    errs() << EC.message() << "\n";
    return true;
  }
  std::unique_ptr<llvm::MemoryBuffer> Code = std::move(CodeOrErr.get());
  if (Code->getBufferSize() == 0)
    return false; // Empty files are formatted correctly.
  std::vector<tooling::Range> Ranges;
  if (fillRanges(Code.get(), Ranges))
    return true;
  StringRef AssumedFileName = (FileName == "-") ? AssumeFileName : FileName;

  llvm::Expected<FormatStyle> FormatStyle =
      getStyle(Style, AssumedFileName, FallbackStyle, Code->getBuffer());
  if (!FormatStyle) {
    llvm::errs() << llvm::toString(FormatStyle.takeError()) << "\n";
    return true;
  }

  if (SortIncludes.getNumOccurrences() != 0)
    FormatStyle->SortIncludes = SortIncludes;
  unsigned CursorPosition = Cursor;
  Replacements Replaces = sortIncludes(*FormatStyle, Code->getBuffer(), Ranges,
                                       AssumedFileName, &CursorPosition);
  auto ChangedCode = tooling::applyAllReplacements(Code->getBuffer(), Replaces);
  if (!ChangedCode) {
    llvm::errs() << llvm::toString(ChangedCode.takeError()) << "\n";
    return true;
  }
  // Get new affected ranges after sorting `#includes`.
  Ranges = tooling::calculateRangesAfterReplacements(Replaces, Ranges);
  FormattingAttemptStatus Status;
  Replacements FormatChanges = reformat(*FormatStyle, *ChangedCode, Ranges,
                                        AssumedFileName, &Status);
  Replaces = Replaces.merge(FormatChanges);
  if (OutputXML) {
    outs() << "<?xml version='1.0'?>\n<replacements "
              "xml:space='preserve' incomplete_format='"
           << (Status.FormatComplete ? "false" : "true") << "'";
    if (!Status.FormatComplete)
      outs() << " line='" << Status.Line << "'";
    outs() << ">\n";
    if (Cursor.getNumOccurrences() != 0)
      outs() << "<cursor>"
             << FormatChanges.getShiftedCodePosition(CursorPosition)
             << "</cursor>\n";

    outputReplacementsXML(Replaces);
    outs() << "</replacements>\n";
  } else {
    IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> InMemoryFileSystem(
        new llvm::vfs::InMemoryFileSystem);
    FileManager Files(FileSystemOptions(), InMemoryFileSystem);
    DiagnosticsEngine Diagnostics(
        IntrusiveRefCntPtr<DiagnosticIDs>(new DiagnosticIDs),
        new DiagnosticOptions);
    SourceManager Sources(Diagnostics, Files);
    FileID ID = createInMemoryFile(AssumedFileName, Code.get(), Sources, Files,
                                   InMemoryFileSystem.get());
    Rewriter Rewrite(Sources, LangOptions());
    tooling::applyAllReplacements(Replaces, Rewrite);
    if (Inplace) {
      if (Rewrite.overwriteChangedFiles())
        return true;
    } else {
      if (Cursor.getNumOccurrences() != 0) {
        outs() << "{ \"Cursor\": "
               << FormatChanges.getShiftedCodePosition(CursorPosition)
               << ", \"IncompleteFormat\": "
               << (Status.FormatComplete ? "false" : "true");
        if (!Status.FormatComplete)
          outs() << ", \"Line\": " << Status.Line;
        outs() << " }\n";
      }
      Rewrite.getEditBuffer(ID).write(outs());
    }
  }
  return false;
}

}  // namespace format
}  // namespace clang

static void PrintVersion(raw_ostream &OS) {
  OS << clang::getClangToolFullVersion("clang-format") << '\n';
}

int main(int argc, const char **argv) {
  llvm::InitLLVM X(argc, argv);

  cl::HideUnrelatedOptions(ClangFormatCategory);

  cl::SetVersionPrinter(PrintVersion);
  cl::ParseCommandLineOptions(
      argc, argv,
      "A tool to format C/C++/Java/JavaScript/Objective-C/Protobuf code.\n\n"
      "If no arguments are specified, it formats the code from standard input\n"
      "and writes the result to the standard output.\n"
      "If <file>s are given, it reformats the files. If -i is specified\n"
      "together with <file>s, the files are edited in-place. Otherwise, the\n"
      "result is written to the standard output.\n");

  if (Help) {
    cl::PrintHelpMessage();
    return 0;
  }

  if (DumpConfig) {
    StringRef FileName;
    std::unique_ptr<llvm::MemoryBuffer> Code;
    if (FileNames.empty()) {
      // We can't read the code to detect the language if there's no
      // file name, so leave Code empty here.
      FileName = AssumeFileName;
    } else {
      // Read in the code in case the filename alone isn't enough to
      // detect the language.
      ErrorOr<std::unique_ptr<MemoryBuffer>> CodeOrErr =
          MemoryBuffer::getFileOrSTDIN(FileNames[0]);
      if (std::error_code EC = CodeOrErr.getError()) {
        llvm::errs() << EC.message() << "\n";
        return 1;
      }
      FileName = (FileNames[0] == "-") ? AssumeFileName : FileNames[0];
      Code = std::move(CodeOrErr.get());
    }
    llvm::Expected<clang::format::FormatStyle> FormatStyle =
        clang::format::getStyle(Style, FileName, FallbackStyle,
                                Code ? Code->getBuffer() : "");
    if (!FormatStyle) {
      llvm::errs() << llvm::toString(FormatStyle.takeError()) << "\n";
      return 1;
    }
    std::string Config = clang::format::configurationAsText(*FormatStyle);
    outs() << Config << "\n";
    return 0;
  }

  bool Error = false;
  if (FileNames.empty()) {
    Error = clang::format::format("-");
    return Error ? 1 : 0;
  }
  if (FileNames.size() != 1 && (!Offsets.empty() || !Lengths.empty() || !LineRanges.empty())) {
    errs() << "error: -offset, -length and -lines can only be used for "
              "single file.\n";
    return 1;
  }
  for (const auto &FileName : FileNames) {
    if (Verbose)
      errs() << "Formatting " << FileName << "\n";
    Error |= clang::format::format(FileName);
  }
  return Error ? 1 : 0;
}
