//===--- AtomicChange.cpp - AtomicChange implementation -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Refactoring/AtomicChange.h"
#include "clang/Tooling/ReplacementsYaml.h"
#include "llvm/Support/YAMLTraits.h"
#include <string>

LLVM_YAML_IS_SEQUENCE_VECTOR(clang::tooling::AtomicChange)

namespace {
/// Helper to (de)serialize an AtomicChange since we don't have direct
/// access to its data members.
/// Data members of a normalized AtomicChange can be directly mapped from/to
/// YAML string.
struct NormalizedAtomicChange {
  NormalizedAtomicChange() = default;

  NormalizedAtomicChange(const llvm::yaml::IO &) {}

  // This converts AtomicChange's internal implementation of the replacements
  // set to a vector of replacements.
  NormalizedAtomicChange(const llvm::yaml::IO &,
                         const clang::tooling::AtomicChange &E)
      : Key(E.getKey()), FilePath(E.getFilePath()), Error(E.getError()),
        InsertedHeaders(E.getInsertedHeaders()),
        RemovedHeaders(E.getRemovedHeaders()),
        Replaces(E.getReplacements().begin(), E.getReplacements().end()) {}

  // This is not expected to be called but needed for template instantiation.
  clang::tooling::AtomicChange denormalize(const llvm::yaml::IO &) {
    llvm_unreachable("Do not convert YAML to AtomicChange directly with '>>'. "
                     "Use AtomicChange::convertFromYAML instead.");
  }
  std::string Key;
  std::string FilePath;
  std::string Error;
  std::vector<std::string> InsertedHeaders;
  std::vector<std::string> RemovedHeaders;
  std::vector<clang::tooling::Replacement> Replaces;
};
} // anonymous namespace

namespace llvm {
namespace yaml {

/// Specialized MappingTraits to describe how an AtomicChange is
/// (de)serialized.
template <> struct MappingTraits<NormalizedAtomicChange> {
  static void mapping(IO &Io, NormalizedAtomicChange &Doc) {
    Io.mapRequired("Key", Doc.Key);
    Io.mapRequired("FilePath", Doc.FilePath);
    Io.mapRequired("Error", Doc.Error);
    Io.mapRequired("InsertedHeaders", Doc.InsertedHeaders);
    Io.mapRequired("RemovedHeaders", Doc.RemovedHeaders);
    Io.mapRequired("Replacements", Doc.Replaces);
  }
};

/// Specialized MappingTraits to describe how an AtomicChange is
/// (de)serialized.
template <> struct MappingTraits<clang::tooling::AtomicChange> {
  static void mapping(IO &Io, clang::tooling::AtomicChange &Doc) {
    MappingNormalization<NormalizedAtomicChange, clang::tooling::AtomicChange>
        Keys(Io, Doc);
    Io.mapRequired("Key", Keys->Key);
    Io.mapRequired("FilePath", Keys->FilePath);
    Io.mapRequired("Error", Keys->Error);
    Io.mapRequired("InsertedHeaders", Keys->InsertedHeaders);
    Io.mapRequired("RemovedHeaders", Keys->RemovedHeaders);
    Io.mapRequired("Replacements", Keys->Replaces);
  }
};

} // end namespace yaml
} // end namespace llvm

namespace clang {
namespace tooling {
namespace {

// Returns true if there is any line that violates \p ColumnLimit in range
// [Start, End].
bool violatesColumnLimit(llvm::StringRef Code, unsigned ColumnLimit,
                         unsigned Start, unsigned End) {
  auto StartPos = Code.rfind('\n', Start);
  StartPos = (StartPos == llvm::StringRef::npos) ? 0 : StartPos + 1;

  auto EndPos = Code.find("\n", End);
  if (EndPos == llvm::StringRef::npos)
    EndPos = Code.size();

  llvm::SmallVector<llvm::StringRef, 8> Lines;
  Code.substr(StartPos, EndPos - StartPos).split(Lines, '\n');
  for (llvm::StringRef Line : Lines)
    if (Line.size() > ColumnLimit)
      return true;
  return false;
}

std::vector<Range>
getRangesForFormating(llvm::StringRef Code, unsigned ColumnLimit,
                      ApplyChangesSpec::FormatOption Format,
                      const clang::tooling::Replacements &Replaces) {
  // kNone suppresses formatting entirely.
  if (Format == ApplyChangesSpec::kNone)
    return {};
  std::vector<clang::tooling::Range> Ranges;
  // This works assuming that replacements are ordered by offset.
  // FIXME: use `getAffectedRanges()` to calculate when it does not include '\n'
  // at the end of an insertion in affected ranges.
  int Offset = 0;
  for (const clang::tooling::Replacement &R : Replaces) {
    int Start = R.getOffset() + Offset;
    int End = Start + R.getReplacementText().size();
    if (!R.getReplacementText().empty() &&
        R.getReplacementText().back() == '\n' && R.getLength() == 0 &&
        R.getOffset() > 0 && R.getOffset() <= Code.size() &&
        Code[R.getOffset() - 1] == '\n')
      // If we are inserting at the start of a line and the replacement ends in
      // a newline, we don't need to format the subsequent line.
      --End;
    Offset += R.getReplacementText().size() - R.getLength();

    if (Format == ApplyChangesSpec::kAll ||
        violatesColumnLimit(Code, ColumnLimit, Start, End))
      Ranges.emplace_back(Start, End - Start);
  }
  return Ranges;
}

inline llvm::Error make_string_error(const llvm::Twine &Message) {
  return llvm::make_error<llvm::StringError>(Message,
                                             llvm::inconvertibleErrorCode());
}

// Creates replacements for inserting/deleting #include headers.
llvm::Expected<Replacements>
createReplacementsForHeaders(llvm::StringRef FilePath, llvm::StringRef Code,
                             llvm::ArrayRef<AtomicChange> Changes,
                             const format::FormatStyle &Style) {
  // Create header insertion/deletion replacements to be cleaned up
  // (i.e. converted to real insertion/deletion replacements).
  Replacements HeaderReplacements;
  for (const auto &Change : Changes) {
    for (llvm::StringRef Header : Change.getInsertedHeaders()) {
      std::string EscapedHeader =
          Header.startswith("<") || Header.startswith("\"")
              ? Header.str()
              : ("\"" + Header + "\"").str();
      std::string ReplacementText = "#include " + EscapedHeader;
      // Offset UINT_MAX and length 0 indicate that the replacement is a header
      // insertion.
      llvm::Error Err = HeaderReplacements.add(
          tooling::Replacement(FilePath, UINT_MAX, 0, ReplacementText));
      if (Err)
        return std::move(Err);
    }
    for (const std::string &Header : Change.getRemovedHeaders()) {
      // Offset UINT_MAX and length 1 indicate that the replacement is a header
      // deletion.
      llvm::Error Err =
          HeaderReplacements.add(Replacement(FilePath, UINT_MAX, 1, Header));
      if (Err)
        return std::move(Err);
    }
  }

  // cleanupAroundReplacements() converts header insertions/deletions into
  // actual replacements that add/remove headers at the right location.
  return clang::format::cleanupAroundReplacements(Code, HeaderReplacements,
                                                  Style);
}

// Combine replacements in all Changes as a `Replacements`. This ignores the
// file path in all replacements and replaces them with \p FilePath.
llvm::Expected<Replacements>
combineReplacementsInChanges(llvm::StringRef FilePath,
                             llvm::ArrayRef<AtomicChange> Changes) {
  Replacements Replaces;
  for (const auto &Change : Changes)
    for (const auto &R : Change.getReplacements())
      if (auto Err = Replaces.add(Replacement(
              FilePath, R.getOffset(), R.getLength(), R.getReplacementText())))
        return std::move(Err);
  return Replaces;
}

} // end namespace

AtomicChange::AtomicChange(const SourceManager &SM,
                           SourceLocation KeyPosition) {
  const FullSourceLoc FullKeyPosition(KeyPosition, SM);
  std::pair<FileID, unsigned> FileIDAndOffset =
      FullKeyPosition.getSpellingLoc().getDecomposedLoc();
  const FileEntry *FE = SM.getFileEntryForID(FileIDAndOffset.first);
  assert(FE && "Cannot create AtomicChange with invalid location.");
  FilePath = FE->getName();
  Key = FilePath + ":" + std::to_string(FileIDAndOffset.second);
}

AtomicChange::AtomicChange(std::string Key, std::string FilePath,
                           std::string Error,
                           std::vector<std::string> InsertedHeaders,
                           std::vector<std::string> RemovedHeaders,
                           clang::tooling::Replacements Replaces)
    : Key(std::move(Key)), FilePath(std::move(FilePath)),
      Error(std::move(Error)), InsertedHeaders(std::move(InsertedHeaders)),
      RemovedHeaders(std::move(RemovedHeaders)), Replaces(std::move(Replaces)) {
}

bool AtomicChange::operator==(const AtomicChange &Other) const {
  if (Key != Other.Key || FilePath != Other.FilePath || Error != Other.Error)
    return false;
  if (!(Replaces == Other.Replaces))
    return false;
  // FXIME: Compare header insertions/removals.
  return true;
}

std::string AtomicChange::toYAMLString() {
  std::string YamlContent;
  llvm::raw_string_ostream YamlContentStream(YamlContent);

  llvm::yaml::Output YAML(YamlContentStream);
  YAML << *this;
  YamlContentStream.flush();
  return YamlContent;
}

AtomicChange AtomicChange::convertFromYAML(llvm::StringRef YAMLContent) {
  NormalizedAtomicChange NE;
  llvm::yaml::Input YAML(YAMLContent);
  YAML >> NE;
  AtomicChange E(NE.Key, NE.FilePath, NE.Error, NE.InsertedHeaders,
                 NE.RemovedHeaders, tooling::Replacements());
  for (const auto &R : NE.Replaces) {
    llvm::Error Err = E.Replaces.add(R);
    if (Err)
      llvm_unreachable(
          "Failed to add replacement when Converting YAML to AtomicChange.");
    llvm::consumeError(std::move(Err));
  }
  return E;
}

llvm::Error AtomicChange::replace(const SourceManager &SM,
                                  const CharSourceRange &Range,
                                  llvm::StringRef ReplacementText) {
  return Replaces.add(Replacement(SM, Range, ReplacementText));
}

llvm::Error AtomicChange::replace(const SourceManager &SM, SourceLocation Loc,
                                  unsigned Length, llvm::StringRef Text) {
  return Replaces.add(Replacement(SM, Loc, Length, Text));
}

llvm::Error AtomicChange::insert(const SourceManager &SM, SourceLocation Loc,
                                 llvm::StringRef Text, bool InsertAfter) {
  if (Text.empty())
    return llvm::Error::success();
  Replacement R(SM, Loc, 0, Text);
  llvm::Error Err = Replaces.add(R);
  if (Err) {
    return llvm::handleErrors(
        std::move(Err), [&](const ReplacementError &RE) -> llvm::Error {
          if (RE.get() != replacement_error::insert_conflict)
            return llvm::make_error<ReplacementError>(RE);
          unsigned NewOffset = Replaces.getShiftedCodePosition(R.getOffset());
          if (!InsertAfter)
            NewOffset -=
                RE.getExistingReplacement()->getReplacementText().size();
          Replacement NewR(R.getFilePath(), NewOffset, 0, Text);
          Replaces = Replaces.merge(Replacements(NewR));
          return llvm::Error::success();
        });
  }
  return llvm::Error::success();
}

void AtomicChange::addHeader(llvm::StringRef Header) {
  InsertedHeaders.push_back(Header);
}

void AtomicChange::removeHeader(llvm::StringRef Header) {
  RemovedHeaders.push_back(Header);
}

llvm::Expected<std::string>
applyAtomicChanges(llvm::StringRef FilePath, llvm::StringRef Code,
                   llvm::ArrayRef<AtomicChange> Changes,
                   const ApplyChangesSpec &Spec) {
  llvm::Expected<Replacements> HeaderReplacements =
      createReplacementsForHeaders(FilePath, Code, Changes, Spec.Style);
  if (!HeaderReplacements)
    return make_string_error(
        "Failed to create replacements for header changes: " +
        llvm::toString(HeaderReplacements.takeError()));

  llvm::Expected<Replacements> Replaces =
      combineReplacementsInChanges(FilePath, Changes);
  if (!Replaces)
    return make_string_error("Failed to combine replacements in all changes: " +
                             llvm::toString(Replaces.takeError()));

  Replacements AllReplaces = std::move(*Replaces);
  for (const auto &R : *HeaderReplacements) {
    llvm::Error Err = AllReplaces.add(R);
    if (Err)
      return make_string_error(
          "Failed to combine existing replacements with header replacements: " +
          llvm::toString(std::move(Err)));
  }

  if (Spec.Cleanup) {
    llvm::Expected<Replacements> CleanReplaces =
        format::cleanupAroundReplacements(Code, AllReplaces, Spec.Style);
    if (!CleanReplaces)
      return make_string_error("Failed to cleanup around replacements: " +
                               llvm::toString(CleanReplaces.takeError()));
    AllReplaces = std::move(*CleanReplaces);
  }

  // Apply all replacements.
  llvm::Expected<std::string> ChangedCode =
      applyAllReplacements(Code, AllReplaces);
  if (!ChangedCode)
    return make_string_error("Failed to apply all replacements: " +
                             llvm::toString(ChangedCode.takeError()));

  // Sort inserted headers. This is done even if other formatting is turned off
  // as incorrectly sorted headers are always just wrong, it's not a matter of
  // taste.
  Replacements HeaderSortingReplacements = format::sortIncludes(
      Spec.Style, *ChangedCode, AllReplaces.getAffectedRanges(), FilePath);
  ChangedCode = applyAllReplacements(*ChangedCode, HeaderSortingReplacements);
  if (!ChangedCode)
    return make_string_error(
        "Failed to apply replacements for sorting includes: " +
        llvm::toString(ChangedCode.takeError()));

  AllReplaces = AllReplaces.merge(HeaderSortingReplacements);

  std::vector<Range> FormatRanges = getRangesForFormating(
      *ChangedCode, Spec.Style.ColumnLimit, Spec.Format, AllReplaces);
  if (!FormatRanges.empty()) {
    Replacements FormatReplacements =
        format::reformat(Spec.Style, *ChangedCode, FormatRanges, FilePath);
    ChangedCode = applyAllReplacements(*ChangedCode, FormatReplacements);
    if (!ChangedCode)
      return make_string_error(
          "Failed to apply replacements for formatting changed code: " +
          llvm::toString(ChangedCode.takeError()));
  }
  return ChangedCode;
}

} // end namespace tooling
} // end namespace clang
