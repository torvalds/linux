//===- InterpolatingCompilationDatabase.cpp ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// InterpolatingCompilationDatabase wraps another CompilationDatabase and
// attempts to heuristically determine appropriate compile commands for files
// that are not included, such as headers or newly created files.
//
// Motivating cases include:
//   Header files that live next to their implementation files. These typically
// share a base filename. (libclang/CXString.h, libclang/CXString.cpp).
//   Some projects separate headers from includes. Filenames still typically
// match, maybe other path segments too. (include/llvm/IR/Use.h, lib/IR/Use.cc).
//   Matches are sometimes only approximate (Sema.h, SemaDecl.cpp). This goes
// for directories too (Support/Unix/Process.inc, lib/Support/Process.cpp).
//   Even if we can't find a "right" compile command, even a random one from
// the project will tend to get important flags like -I and -x right.
//
// We "borrow" the compile command for the closest available file:
//   - points are awarded if the filename matches (ignoring extension)
//   - points are awarded if the directory structure matches
//   - ties are broken by length of path prefix match
//
// The compile command is adjusted, replacing the filename and removing output
// file arguments. The -x and -std flags may be affected too.
//
// Source language is a tricky issue: is it OK to use a .c file's command
// for building a .cc file? What language is a .h file in?
//   - We only consider compile commands for c-family languages as candidates.
//   - For files whose language is implied by the filename (e.g. .m, .hpp)
//     we prefer candidates from the same language.
//     If we must cross languages, we drop any -x and -std flags.
//   - For .h files, candidates from any c-family language are acceptable.
//     We use the candidate's language, inserting  e.g. -x c++-header.
//
// This class is only useful when wrapping databases that can enumerate all
// their compile commands. If getAllFilenames() is empty, no inference occurs.
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Options.h"
#include "clang/Driver/Types.h"
#include "clang/Frontend/LangStandard.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

namespace clang {
namespace tooling {
namespace {
using namespace llvm;
namespace types = clang::driver::types;
namespace path = llvm::sys::path;

// The length of the prefix these two strings have in common.
size_t matchingPrefix(StringRef L, StringRef R) {
  size_t Limit = std::min(L.size(), R.size());
  for (size_t I = 0; I < Limit; ++I)
    if (L[I] != R[I])
      return I;
  return Limit;
}

// A comparator for searching SubstringWithIndexes with std::equal_range etc.
// Optionaly prefix semantics: compares equal if the key is a prefix.
template <bool Prefix> struct Less {
  bool operator()(StringRef Key, std::pair<StringRef, size_t> Value) const {
    StringRef V = Prefix ? Value.first.substr(0, Key.size()) : Value.first;
    return Key < V;
  }
  bool operator()(std::pair<StringRef, size_t> Value, StringRef Key) const {
    StringRef V = Prefix ? Value.first.substr(0, Key.size()) : Value.first;
    return V < Key;
  }
};

// Infer type from filename. If we might have gotten it wrong, set *Certain.
// *.h will be inferred as a C header, but not certain.
types::ID guessType(StringRef Filename, bool *Certain = nullptr) {
  // path::extension is ".cpp", lookupTypeForExtension wants "cpp".
  auto Lang =
      types::lookupTypeForExtension(path::extension(Filename).substr(1));
  if (Certain)
    *Certain = Lang != types::TY_CHeader && Lang != types::TY_INVALID;
  return Lang;
}

// Return Lang as one of the canonical supported types.
// e.g. c-header --> c; fortran --> TY_INVALID
static types::ID foldType(types::ID Lang) {
  switch (Lang) {
  case types::TY_C:
  case types::TY_CHeader:
    return types::TY_C;
  case types::TY_ObjC:
  case types::TY_ObjCHeader:
    return types::TY_ObjC;
  case types::TY_CXX:
  case types::TY_CXXHeader:
    return types::TY_CXX;
  case types::TY_ObjCXX:
  case types::TY_ObjCXXHeader:
    return types::TY_ObjCXX;
  default:
    return types::TY_INVALID;
  }
}

// A CompileCommand that can be applied to another file.
struct TransferableCommand {
  // Flags that should not apply to all files are stripped from CommandLine.
  CompileCommand Cmd;
  // Language detected from -x or the filename. Never TY_INVALID.
  Optional<types::ID> Type;
  // Standard specified by -std.
  LangStandard::Kind Std = LangStandard::lang_unspecified;
  // Whether the command line is for the cl-compatible driver.
  bool ClangCLMode;

  TransferableCommand(CompileCommand C)
      : Cmd(std::move(C)), Type(guessType(Cmd.Filename)),
        ClangCLMode(checkIsCLMode(Cmd.CommandLine)) {
    std::vector<std::string> OldArgs = std::move(Cmd.CommandLine);
    Cmd.CommandLine.clear();

    // Wrap the old arguments in an InputArgList.
    llvm::opt::InputArgList ArgList;
    {
      SmallVector<const char *, 16> TmpArgv;
      for (const std::string &S : OldArgs)
        TmpArgv.push_back(S.c_str());
      ArgList = {TmpArgv.begin(), TmpArgv.end()};
    }

    // Parse the old args in order to strip out and record unwanted flags.
    // We parse each argument individually so that we can retain the exact
    // spelling of each argument; re-rendering is lossy for aliased flags.
    // E.g. in CL mode, /W4 maps to -Wall.
    auto OptTable = clang::driver::createDriverOptTable();
    Cmd.CommandLine.emplace_back(OldArgs.front());
    for (unsigned Pos = 1; Pos < OldArgs.size();) {
      using namespace driver::options;

      const unsigned OldPos = Pos;
      std::unique_ptr<llvm::opt::Arg> Arg(OptTable->ParseOneArg(
          ArgList, Pos,
          /* Include */ClangCLMode ? CoreOption | CLOption : 0,
          /* Exclude */ClangCLMode ? 0 : CLOption));

      if (!Arg)
        continue;

      const llvm::opt::Option &Opt = Arg->getOption();

      // Strip input and output files.
      if (Opt.matches(OPT_INPUT) || Opt.matches(OPT_o) ||
          (ClangCLMode && (Opt.matches(OPT__SLASH_Fa) ||
                           Opt.matches(OPT__SLASH_Fe) ||
                           Opt.matches(OPT__SLASH_Fi) ||
                           Opt.matches(OPT__SLASH_Fo))))
        continue;

      // Strip -x, but record the overridden language.
      if (const auto GivenType = tryParseTypeArg(*Arg)) {
        Type = *GivenType;
        continue;
      }

      // Strip -std, but record the value.
      if (const auto GivenStd = tryParseStdArg(*Arg)) {
        if (*GivenStd != LangStandard::lang_unspecified)
          Std = *GivenStd;
        continue;
      }

      Cmd.CommandLine.insert(Cmd.CommandLine.end(),
                             OldArgs.data() + OldPos, OldArgs.data() + Pos);
    }

    if (Std != LangStandard::lang_unspecified) // -std take precedence over -x
      Type = toType(LangStandard::getLangStandardForKind(Std).getLanguage());
    Type = foldType(*Type);
    // The contract is to store None instead of TY_INVALID.
    if (Type == types::TY_INVALID)
      Type = llvm::None;
  }

  // Produce a CompileCommand for \p filename, based on this one.
  CompileCommand transferTo(StringRef Filename) const {
    CompileCommand Result = Cmd;
    Result.Filename = Filename;
    bool TypeCertain;
    auto TargetType = guessType(Filename, &TypeCertain);
    // If the filename doesn't determine the language (.h), transfer with -x.
    if (TargetType != types::TY_INVALID && !TypeCertain && Type) {
      TargetType = types::onlyPrecompileType(TargetType) // header?
                       ? types::lookupHeaderTypeForSourceType(*Type)
                       : *Type;
      if (ClangCLMode) {
        const StringRef Flag = toCLFlag(TargetType);
        if (!Flag.empty())
          Result.CommandLine.push_back(Flag);
      } else {
        Result.CommandLine.push_back("-x");
        Result.CommandLine.push_back(types::getTypeName(TargetType));
      }
    }
    // --std flag may only be transferred if the language is the same.
    // We may consider "translating" these, e.g. c++11 -> c11.
    if (Std != LangStandard::lang_unspecified && foldType(TargetType) == Type) {
      Result.CommandLine.emplace_back((
          llvm::Twine(ClangCLMode ? "/std:" : "-std=") +
          LangStandard::getLangStandardForKind(Std).getName()).str());
    }
    Result.CommandLine.push_back(Filename);
    return Result;
  }

private:
  // Determine whether the given command line is intended for the CL driver.
  static bool checkIsCLMode(ArrayRef<std::string> CmdLine) {
    // First look for --driver-mode.
    for (StringRef S : llvm::reverse(CmdLine)) {
      if (S.consume_front("--driver-mode="))
        return S == "cl";
    }

    // Otherwise just check the clang executable file name.
    return llvm::sys::path::stem(CmdLine.front()).endswith_lower("cl");
  }

  // Map the language from the --std flag to that of the -x flag.
  static types::ID toType(InputKind::Language Lang) {
    switch (Lang) {
    case InputKind::C:
      return types::TY_C;
    case InputKind::CXX:
      return types::TY_CXX;
    case InputKind::ObjC:
      return types::TY_ObjC;
    case InputKind::ObjCXX:
      return types::TY_ObjCXX;
    default:
      return types::TY_INVALID;
    }
  }

  // Convert a file type to the matching CL-style type flag.
  static StringRef toCLFlag(types::ID Type) {
    switch (Type) {
    case types::TY_C:
    case types::TY_CHeader:
      return "/TC";
    case types::TY_CXX:
    case types::TY_CXXHeader:
      return "/TP";
    default:
      return StringRef();
    }
  }

  // Try to interpret the argument as a type specifier, e.g. '-x'.
  Optional<types::ID> tryParseTypeArg(const llvm::opt::Arg &Arg) {
    const llvm::opt::Option &Opt = Arg.getOption();
    using namespace driver::options;
    if (ClangCLMode) {
      if (Opt.matches(OPT__SLASH_TC) || Opt.matches(OPT__SLASH_Tc))
        return types::TY_C;
      if (Opt.matches(OPT__SLASH_TP) || Opt.matches(OPT__SLASH_Tp))
        return types::TY_CXX;
    } else {
      if (Opt.matches(driver::options::OPT_x))
        return types::lookupTypeForTypeSpecifier(Arg.getValue());
    }
    return None;
  }

  // Try to interpret the argument as '-std='.
  Optional<LangStandard::Kind> tryParseStdArg(const llvm::opt::Arg &Arg) {
    using namespace driver::options;
    if (Arg.getOption().matches(ClangCLMode ? OPT__SLASH_std : OPT_std_EQ)) {
      return llvm::StringSwitch<LangStandard::Kind>(Arg.getValue())
#define LANGSTANDARD(id, name, lang, ...) .Case(name, LangStandard::lang_##id)
#define LANGSTANDARD_ALIAS(id, alias) .Case(alias, LangStandard::lang_##id)
#include "clang/Frontend/LangStandards.def"
#undef LANGSTANDARD_ALIAS
#undef LANGSTANDARD
                 .Default(LangStandard::lang_unspecified);
    }
    return None;
  }
};

// Given a filename, FileIndex picks the best matching file from the underlying
// DB. This is the proxy file whose CompileCommand will be reused. The
// heuristics incorporate file name, extension, and directory structure.
// Strategy:
// - Build indexes of each of the substrings we want to look up by.
//   These indexes are just sorted lists of the substrings.
// - Each criterion corresponds to a range lookup into the index, so we only
//   need O(log N) string comparisons to determine scores.
//
// Apart from path proximity signals, also takes file extensions into account
// when scoring the candidates.
class FileIndex {
public:
  FileIndex(std::vector<std::string> Files)
      : OriginalPaths(std::move(Files)), Strings(Arena) {
    // Sort commands by filename for determinism (index is a tiebreaker later).
    llvm::sort(OriginalPaths);
    Paths.reserve(OriginalPaths.size());
    Types.reserve(OriginalPaths.size());
    Stems.reserve(OriginalPaths.size());
    for (size_t I = 0; I < OriginalPaths.size(); ++I) {
      StringRef Path = Strings.save(StringRef(OriginalPaths[I]).lower());

      Paths.emplace_back(Path, I);
      Types.push_back(foldType(guessType(Path)));
      Stems.emplace_back(sys::path::stem(Path), I);
      auto Dir = ++sys::path::rbegin(Path), DirEnd = sys::path::rend(Path);
      for (int J = 0; J < DirectorySegmentsIndexed && Dir != DirEnd; ++J, ++Dir)
        if (Dir->size() > ShortDirectorySegment) // not trivial ones
          Components.emplace_back(*Dir, I);
    }
    llvm::sort(Paths);
    llvm::sort(Stems);
    llvm::sort(Components);
  }

  bool empty() const { return Paths.empty(); }

  // Returns the path for the file that best fits OriginalFilename.
  // Candidates with extensions matching PreferLanguage will be chosen over
  // others (unless it's TY_INVALID, or all candidates are bad).
  StringRef chooseProxy(StringRef OriginalFilename,
                        types::ID PreferLanguage) const {
    assert(!empty() && "need at least one candidate!");
    std::string Filename = OriginalFilename.lower();
    auto Candidates = scoreCandidates(Filename);
    std::pair<size_t, int> Best =
        pickWinner(Candidates, Filename, PreferLanguage);

    DEBUG_WITH_TYPE(
        "interpolate",
        llvm::dbgs() << "interpolate: chose " << OriginalPaths[Best.first]
                     << " as proxy for " << OriginalFilename << " preferring "
                     << (PreferLanguage == types::TY_INVALID
                             ? "none"
                             : types::getTypeName(PreferLanguage))
                     << " score=" << Best.second << "\n");
    return OriginalPaths[Best.first];
  }

private:
  using SubstringAndIndex = std::pair<StringRef, size_t>;
  // Directory matching parameters: we look at the last two segments of the
  // parent directory (usually the semantically significant ones in practice).
  // We search only the last four of each candidate (for efficiency).
  constexpr static int DirectorySegmentsIndexed = 4;
  constexpr static int DirectorySegmentsQueried = 2;
  constexpr static int ShortDirectorySegment = 1; // Only look at longer names.

  // Award points to candidate entries that should be considered for the file.
  // Returned keys are indexes into paths, and the values are (nonzero) scores.
  DenseMap<size_t, int> scoreCandidates(StringRef Filename) const {
    // Decompose Filename into the parts we care about.
    // /some/path/complicated/project/Interesting.h
    // [-prefix--][---dir---] [-dir-] [--stem---]
    StringRef Stem = sys::path::stem(Filename);
    llvm::SmallVector<StringRef, DirectorySegmentsQueried> Dirs;
    llvm::StringRef Prefix;
    auto Dir = ++sys::path::rbegin(Filename),
         DirEnd = sys::path::rend(Filename);
    for (int I = 0; I < DirectorySegmentsQueried && Dir != DirEnd; ++I, ++Dir) {
      if (Dir->size() > ShortDirectorySegment)
        Dirs.push_back(*Dir);
      Prefix = Filename.substr(0, Dir - DirEnd);
    }

    // Now award points based on lookups into our various indexes.
    DenseMap<size_t, int> Candidates; // Index -> score.
    auto Award = [&](int Points, ArrayRef<SubstringAndIndex> Range) {
      for (const auto &Entry : Range)
        Candidates[Entry.second] += Points;
    };
    // Award one point if the file's basename is a prefix of the candidate,
    // and another if it's an exact match (so exact matches get two points).
    Award(1, indexLookup</*Prefix=*/true>(Stem, Stems));
    Award(1, indexLookup</*Prefix=*/false>(Stem, Stems));
    // For each of the last few directories in the Filename, award a point
    // if it's present in the candidate.
    for (StringRef Dir : Dirs)
      Award(1, indexLookup</*Prefix=*/false>(Dir, Components));
    // Award one more point if the whole rest of the path matches.
    if (sys::path::root_directory(Prefix) != Prefix)
      Award(1, indexLookup</*Prefix=*/true>(Prefix, Paths));
    return Candidates;
  }

  // Pick a single winner from the set of scored candidates.
  // Returns (index, score).
  std::pair<size_t, int> pickWinner(const DenseMap<size_t, int> &Candidates,
                                    StringRef Filename,
                                    types::ID PreferredLanguage) const {
    struct ScoredCandidate {
      size_t Index;
      bool Preferred;
      int Points;
      size_t PrefixLength;
    };
    // Choose the best candidate by (preferred, points, prefix length, alpha).
    ScoredCandidate Best = {size_t(-1), false, 0, 0};
    for (const auto &Candidate : Candidates) {
      ScoredCandidate S;
      S.Index = Candidate.first;
      S.Preferred = PreferredLanguage == types::TY_INVALID ||
                    PreferredLanguage == Types[S.Index];
      S.Points = Candidate.second;
      if (!S.Preferred && Best.Preferred)
        continue;
      if (S.Preferred == Best.Preferred) {
        if (S.Points < Best.Points)
          continue;
        if (S.Points == Best.Points) {
          S.PrefixLength = matchingPrefix(Filename, Paths[S.Index].first);
          if (S.PrefixLength < Best.PrefixLength)
            continue;
          // hidden heuristics should at least be deterministic!
          if (S.PrefixLength == Best.PrefixLength)
            if (S.Index > Best.Index)
              continue;
        }
      }
      // PrefixLength was only set above if actually needed for a tiebreak.
      // But it definitely needs to be set to break ties in the future.
      S.PrefixLength = matchingPrefix(Filename, Paths[S.Index].first);
      Best = S;
    }
    // Edge case: no candidate got any points.
    // We ignore PreferredLanguage at this point (not ideal).
    if (Best.Index == size_t(-1))
      return {longestMatch(Filename, Paths).second, 0};
    return {Best.Index, Best.Points};
  }

  // Returns the range within a sorted index that compares equal to Key.
  // If Prefix is true, it's instead the range starting with Key.
  template <bool Prefix>
  ArrayRef<SubstringAndIndex>
  indexLookup(StringRef Key, ArrayRef<SubstringAndIndex> Idx) const {
    // Use pointers as iteratiors to ease conversion of result to ArrayRef.
    auto Range = std::equal_range(Idx.data(), Idx.data() + Idx.size(), Key,
                                  Less<Prefix>());
    return {Range.first, Range.second};
  }

  // Performs a point lookup into a nonempty index, returning a longest match.
  SubstringAndIndex longestMatch(StringRef Key,
                                 ArrayRef<SubstringAndIndex> Idx) const {
    assert(!Idx.empty());
    // Longest substring match will be adjacent to a direct lookup.
    auto It =
        std::lower_bound(Idx.begin(), Idx.end(), SubstringAndIndex{Key, 0});
    if (It == Idx.begin())
      return *It;
    if (It == Idx.end())
      return *--It;
    // Have to choose between It and It-1
    size_t Prefix = matchingPrefix(Key, It->first);
    size_t PrevPrefix = matchingPrefix(Key, (It - 1)->first);
    return Prefix > PrevPrefix ? *It : *--It;
  }

  // Original paths, everything else is in lowercase.
  std::vector<std::string> OriginalPaths;
  BumpPtrAllocator Arena;
  StringSaver Strings;
  // Indexes of candidates by certain substrings.
  // String is lowercase and sorted, index points into OriginalPaths.
  std::vector<SubstringAndIndex> Paths;      // Full path.
  // Lang types obtained by guessing on the corresponding path. I-th element is
  // a type for the I-th path.
  std::vector<types::ID> Types;
  std::vector<SubstringAndIndex> Stems;      // Basename, without extension.
  std::vector<SubstringAndIndex> Components; // Last path components.
};

// The actual CompilationDatabase wrapper delegates to its inner database.
// If no match, looks up a proxy file in FileIndex and transfers its
// command to the requested file.
class InterpolatingCompilationDatabase : public CompilationDatabase {
public:
  InterpolatingCompilationDatabase(std::unique_ptr<CompilationDatabase> Inner)
      : Inner(std::move(Inner)), Index(this->Inner->getAllFiles()) {}

  std::vector<CompileCommand>
  getCompileCommands(StringRef Filename) const override {
    auto Known = Inner->getCompileCommands(Filename);
    if (Index.empty() || !Known.empty())
      return Known;
    bool TypeCertain;
    auto Lang = guessType(Filename, &TypeCertain);
    if (!TypeCertain)
      Lang = types::TY_INVALID;
    auto ProxyCommands =
        Inner->getCompileCommands(Index.chooseProxy(Filename, foldType(Lang)));
    if (ProxyCommands.empty())
      return {};
    return {TransferableCommand(ProxyCommands[0]).transferTo(Filename)};
  }

  std::vector<std::string> getAllFiles() const override {
    return Inner->getAllFiles();
  }

  std::vector<CompileCommand> getAllCompileCommands() const override {
    return Inner->getAllCompileCommands();
  }

private:
  std::unique_ptr<CompilationDatabase> Inner;
  FileIndex Index;
};

} // namespace

std::unique_ptr<CompilationDatabase>
inferMissingCompileCommands(std::unique_ptr<CompilationDatabase> Inner) {
  return llvm::make_unique<InterpolatingCompilationDatabase>(std::move(Inner));
}

} // namespace tooling
} // namespace clang
