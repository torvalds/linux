//===--- Availability.h - Classes for availability --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This files defines some classes that implement availability checking.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_AVAILABILITY_H
#define LLVM_CLANG_AST_AVAILABILITY_H

#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/VersionTuple.h"

namespace clang {

/// One specifier in an @available expression.
///
/// \code
///   @available(macos 10.10, *)
/// \endcode
///
/// Here, 'macos 10.10' and '*' both map to an instance of this type.
///
class AvailabilitySpec {
  /// Represents the version that this specifier requires. If the host OS
  /// version is greater than or equal to Version, the @available will evaluate
  /// to true.
  VersionTuple Version;

  /// Name of the platform that Version corresponds to.
  StringRef Platform;

  SourceLocation BeginLoc, EndLoc;

public:
  AvailabilitySpec(VersionTuple Version, StringRef Platform,
                   SourceLocation BeginLoc, SourceLocation EndLoc)
      : Version(Version), Platform(Platform), BeginLoc(BeginLoc),
        EndLoc(EndLoc) {}

  /// This constructor is used when representing the '*' case.
  AvailabilitySpec(SourceLocation StarLoc)
      : BeginLoc(StarLoc), EndLoc(StarLoc) {}

  VersionTuple getVersion() const { return Version; }
  StringRef getPlatform() const { return Platform; }
  SourceLocation getBeginLoc() const { return BeginLoc; }
  SourceLocation getEndLoc() const { return EndLoc; }

  /// Returns true when this represents the '*' case.
  bool isOtherPlatformSpec() const { return Version.empty(); }
};

} // end namespace clang

#endif
