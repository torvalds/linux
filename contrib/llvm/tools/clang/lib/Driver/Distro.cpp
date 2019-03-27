//===--- Distro.cpp - Linux distribution detection support ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Distro.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace clang::driver;
using namespace clang;

static Distro::DistroType DetectDistro(llvm::vfs::FileSystem &VFS) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> File =
      VFS.getBufferForFile("/etc/lsb-release");
  if (File) {
    StringRef Data = File.get()->getBuffer();
    SmallVector<StringRef, 16> Lines;
    Data.split(Lines, "\n");
    Distro::DistroType Version = Distro::UnknownDistro;
    for (StringRef Line : Lines)
      if (Version == Distro::UnknownDistro && Line.startswith("DISTRIB_CODENAME="))
        Version = llvm::StringSwitch<Distro::DistroType>(Line.substr(17))
                      .Case("hardy", Distro::UbuntuHardy)
                      .Case("intrepid", Distro::UbuntuIntrepid)
                      .Case("jaunty", Distro::UbuntuJaunty)
                      .Case("karmic", Distro::UbuntuKarmic)
                      .Case("lucid", Distro::UbuntuLucid)
                      .Case("maverick", Distro::UbuntuMaverick)
                      .Case("natty", Distro::UbuntuNatty)
                      .Case("oneiric", Distro::UbuntuOneiric)
                      .Case("precise", Distro::UbuntuPrecise)
                      .Case("quantal", Distro::UbuntuQuantal)
                      .Case("raring", Distro::UbuntuRaring)
                      .Case("saucy", Distro::UbuntuSaucy)
                      .Case("trusty", Distro::UbuntuTrusty)
                      .Case("utopic", Distro::UbuntuUtopic)
                      .Case("vivid", Distro::UbuntuVivid)
                      .Case("wily", Distro::UbuntuWily)
                      .Case("xenial", Distro::UbuntuXenial)
                      .Case("yakkety", Distro::UbuntuYakkety)
                      .Case("zesty", Distro::UbuntuZesty)
                      .Case("artful", Distro::UbuntuArtful)
                      .Case("bionic", Distro::UbuntuBionic)
                      .Case("cosmic", Distro::UbuntuCosmic)
                      .Case("disco", Distro::UbuntuDisco)
                      .Default(Distro::UnknownDistro);
    if (Version != Distro::UnknownDistro)
      return Version;
  }

  File = VFS.getBufferForFile("/etc/redhat-release");
  if (File) {
    StringRef Data = File.get()->getBuffer();
    if (Data.startswith("Fedora release"))
      return Distro::Fedora;
    if (Data.startswith("Red Hat Enterprise Linux") ||
        Data.startswith("CentOS") ||
        Data.startswith("Scientific Linux")) {
      if (Data.find("release 7") != StringRef::npos)
        return Distro::RHEL7;
      else if (Data.find("release 6") != StringRef::npos)
        return Distro::RHEL6;
      else if (Data.find("release 5") != StringRef::npos)
        return Distro::RHEL5;
    }
    return Distro::UnknownDistro;
  }

  File = VFS.getBufferForFile("/etc/debian_version");
  if (File) {
    StringRef Data = File.get()->getBuffer();
    // Contents: < major.minor > or < codename/sid >
    int MajorVersion;
    if (!Data.split('.').first.getAsInteger(10, MajorVersion)) {
      switch (MajorVersion) {
      case 5:
        return Distro::DebianLenny;
      case 6:
        return Distro::DebianSqueeze;
      case 7:
        return Distro::DebianWheezy;
      case 8:
        return Distro::DebianJessie;
      case 9:
        return Distro::DebianStretch;
      case 10:
        return Distro::DebianBuster;
      default:
        return Distro::UnknownDistro;
      }
    }
    return llvm::StringSwitch<Distro::DistroType>(Data.split("\n").first)
        .Case("squeeze/sid", Distro::DebianSqueeze)
        .Case("wheezy/sid", Distro::DebianWheezy)
        .Case("jessie/sid", Distro::DebianJessie)
        .Case("stretch/sid", Distro::DebianStretch)
        .Default(Distro::UnknownDistro);
  }

  File = VFS.getBufferForFile("/etc/SuSE-release");
  if (File) {
    StringRef Data = File.get()->getBuffer();
    SmallVector<StringRef, 8> Lines;
    Data.split(Lines, "\n");
    for (const StringRef& Line : Lines) {
      if (!Line.trim().startswith("VERSION"))
        continue;
      std::pair<StringRef, StringRef> SplitLine = Line.split('=');
      // Old versions have split VERSION and PATCHLEVEL
      // Newer versions use VERSION = x.y
      std::pair<StringRef, StringRef> SplitVer = SplitLine.second.trim().split('.');
      int Version;

      // OpenSUSE/SLES 10 and older are not supported and not compatible
      // with our rules, so just treat them as Distro::UnknownDistro.
      if (!SplitVer.first.getAsInteger(10, Version) && Version > 10)
        return Distro::OpenSUSE;
      return Distro::UnknownDistro;
    }
    return Distro::UnknownDistro;
  }

  if (VFS.exists("/etc/exherbo-release"))
    return Distro::Exherbo;

  if (VFS.exists("/etc/alpine-release"))
    return Distro::AlpineLinux;

  if (VFS.exists("/etc/arch-release"))
    return Distro::ArchLinux;

  if (VFS.exists("/etc/gentoo-release"))
    return Distro::Gentoo;

  return Distro::UnknownDistro;
}

Distro::Distro(llvm::vfs::FileSystem &VFS) : DistroVal(DetectDistro(VFS)) {}
