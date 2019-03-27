//===-- llvm/ModuleSummaryIndexYAML.h - YAML I/O for summary ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_MODULESUMMARYINDEXYAML_H
#define LLVM_IR_MODULESUMMARYINDEXYAML_H

#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Support/YAMLTraits.h"

namespace llvm {
namespace yaml {

template <> struct ScalarEnumerationTraits<TypeTestResolution::Kind> {
  static void enumeration(IO &io, TypeTestResolution::Kind &value) {
    io.enumCase(value, "Unsat", TypeTestResolution::Unsat);
    io.enumCase(value, "ByteArray", TypeTestResolution::ByteArray);
    io.enumCase(value, "Inline", TypeTestResolution::Inline);
    io.enumCase(value, "Single", TypeTestResolution::Single);
    io.enumCase(value, "AllOnes", TypeTestResolution::AllOnes);
  }
};

template <> struct MappingTraits<TypeTestResolution> {
  static void mapping(IO &io, TypeTestResolution &res) {
    io.mapOptional("Kind", res.TheKind);
    io.mapOptional("SizeM1BitWidth", res.SizeM1BitWidth);
    io.mapOptional("AlignLog2", res.AlignLog2);
    io.mapOptional("SizeM1", res.SizeM1);
    io.mapOptional("BitMask", res.BitMask);
    io.mapOptional("InlineBits", res.InlineBits);
  }
};

template <>
struct ScalarEnumerationTraits<WholeProgramDevirtResolution::ByArg::Kind> {
  static void enumeration(IO &io,
                          WholeProgramDevirtResolution::ByArg::Kind &value) {
    io.enumCase(value, "Indir", WholeProgramDevirtResolution::ByArg::Indir);
    io.enumCase(value, "UniformRetVal",
                WholeProgramDevirtResolution::ByArg::UniformRetVal);
    io.enumCase(value, "UniqueRetVal",
                WholeProgramDevirtResolution::ByArg::UniqueRetVal);
    io.enumCase(value, "VirtualConstProp",
                WholeProgramDevirtResolution::ByArg::VirtualConstProp);
  }
};

template <> struct MappingTraits<WholeProgramDevirtResolution::ByArg> {
  static void mapping(IO &io, WholeProgramDevirtResolution::ByArg &res) {
    io.mapOptional("Kind", res.TheKind);
    io.mapOptional("Info", res.Info);
    io.mapOptional("Byte", res.Byte);
    io.mapOptional("Bit", res.Bit);
  }
};

template <>
struct CustomMappingTraits<
    std::map<std::vector<uint64_t>, WholeProgramDevirtResolution::ByArg>> {
  static void inputOne(
      IO &io, StringRef Key,
      std::map<std::vector<uint64_t>, WholeProgramDevirtResolution::ByArg> &V) {
    std::vector<uint64_t> Args;
    std::pair<StringRef, StringRef> P = {"", Key};
    while (!P.second.empty()) {
      P = P.second.split(',');
      uint64_t Arg;
      if (P.first.getAsInteger(0, Arg)) {
        io.setError("key not an integer");
        return;
      }
      Args.push_back(Arg);
    }
    io.mapRequired(Key.str().c_str(), V[Args]);
  }
  static void output(
      IO &io,
      std::map<std::vector<uint64_t>, WholeProgramDevirtResolution::ByArg> &V) {
    for (auto &P : V) {
      std::string Key;
      for (uint64_t Arg : P.first) {
        if (!Key.empty())
          Key += ',';
        Key += llvm::utostr(Arg);
      }
      io.mapRequired(Key.c_str(), P.second);
    }
  }
};

template <> struct ScalarEnumerationTraits<WholeProgramDevirtResolution::Kind> {
  static void enumeration(IO &io, WholeProgramDevirtResolution::Kind &value) {
    io.enumCase(value, "Indir", WholeProgramDevirtResolution::Indir);
    io.enumCase(value, "SingleImpl", WholeProgramDevirtResolution::SingleImpl);
    io.enumCase(value, "BranchFunnel",
                WholeProgramDevirtResolution::BranchFunnel);
  }
};

template <> struct MappingTraits<WholeProgramDevirtResolution> {
  static void mapping(IO &io, WholeProgramDevirtResolution &res) {
    io.mapOptional("Kind", res.TheKind);
    io.mapOptional("SingleImplName", res.SingleImplName);
    io.mapOptional("ResByArg", res.ResByArg);
  }
};

template <>
struct CustomMappingTraits<std::map<uint64_t, WholeProgramDevirtResolution>> {
  static void inputOne(IO &io, StringRef Key,
                       std::map<uint64_t, WholeProgramDevirtResolution> &V) {
    uint64_t KeyInt;
    if (Key.getAsInteger(0, KeyInt)) {
      io.setError("key not an integer");
      return;
    }
    io.mapRequired(Key.str().c_str(), V[KeyInt]);
  }
  static void output(IO &io, std::map<uint64_t, WholeProgramDevirtResolution> &V) {
    for (auto &P : V)
      io.mapRequired(llvm::utostr(P.first).c_str(), P.second);
  }
};

template <> struct MappingTraits<TypeIdSummary> {
  static void mapping(IO &io, TypeIdSummary& summary) {
    io.mapOptional("TTRes", summary.TTRes);
    io.mapOptional("WPDRes", summary.WPDRes);
  }
};

struct FunctionSummaryYaml {
  unsigned Linkage;
  bool NotEligibleToImport, Live, IsLocal;
  std::vector<uint64_t> Refs;
  std::vector<uint64_t> TypeTests;
  std::vector<FunctionSummary::VFuncId> TypeTestAssumeVCalls,
      TypeCheckedLoadVCalls;
  std::vector<FunctionSummary::ConstVCall> TypeTestAssumeConstVCalls,
      TypeCheckedLoadConstVCalls;
};

} // End yaml namespace
} // End llvm namespace

namespace llvm {
namespace yaml {

template <> struct MappingTraits<FunctionSummary::VFuncId> {
  static void mapping(IO &io, FunctionSummary::VFuncId& id) {
    io.mapOptional("GUID", id.GUID);
    io.mapOptional("Offset", id.Offset);
  }
};

template <> struct MappingTraits<FunctionSummary::ConstVCall> {
  static void mapping(IO &io, FunctionSummary::ConstVCall& id) {
    io.mapOptional("VFunc", id.VFunc);
    io.mapOptional("Args", id.Args);
  }
};

} // End yaml namespace
} // End llvm namespace

LLVM_YAML_IS_SEQUENCE_VECTOR(FunctionSummary::VFuncId)
LLVM_YAML_IS_SEQUENCE_VECTOR(FunctionSummary::ConstVCall)

namespace llvm {
namespace yaml {

template <> struct MappingTraits<FunctionSummaryYaml> {
  static void mapping(IO &io, FunctionSummaryYaml& summary) {
    io.mapOptional("Linkage", summary.Linkage);
    io.mapOptional("NotEligibleToImport", summary.NotEligibleToImport);
    io.mapOptional("Live", summary.Live);
    io.mapOptional("Local", summary.IsLocal);
    io.mapOptional("Refs", summary.Refs);
    io.mapOptional("TypeTests", summary.TypeTests);
    io.mapOptional("TypeTestAssumeVCalls", summary.TypeTestAssumeVCalls);
    io.mapOptional("TypeCheckedLoadVCalls", summary.TypeCheckedLoadVCalls);
    io.mapOptional("TypeTestAssumeConstVCalls",
                   summary.TypeTestAssumeConstVCalls);
    io.mapOptional("TypeCheckedLoadConstVCalls",
                   summary.TypeCheckedLoadConstVCalls);
  }
};

} // End yaml namespace
} // End llvm namespace

LLVM_YAML_IS_SEQUENCE_VECTOR(FunctionSummaryYaml)

namespace llvm {
namespace yaml {

// FIXME: Add YAML mappings for the rest of the module summary.
template <> struct CustomMappingTraits<GlobalValueSummaryMapTy> {
  static void inputOne(IO &io, StringRef Key, GlobalValueSummaryMapTy &V) {
    std::vector<FunctionSummaryYaml> FSums;
    io.mapRequired(Key.str().c_str(), FSums);
    uint64_t KeyInt;
    if (Key.getAsInteger(0, KeyInt)) {
      io.setError("key not an integer");
      return;
    }
    if (!V.count(KeyInt))
      V.emplace(KeyInt, /*IsAnalysis=*/false);
    auto &Elem = V.find(KeyInt)->second;
    for (auto &FSum : FSums) {
      std::vector<ValueInfo> Refs;
      for (auto &RefGUID : FSum.Refs) {
        if (!V.count(RefGUID))
          V.emplace(RefGUID, /*IsAnalysis=*/false);
        Refs.push_back(ValueInfo(/*IsAnalysis=*/false, &*V.find(RefGUID)));
      }
      Elem.SummaryList.push_back(llvm::make_unique<FunctionSummary>(
          GlobalValueSummary::GVFlags(
              static_cast<GlobalValue::LinkageTypes>(FSum.Linkage),
              FSum.NotEligibleToImport, FSum.Live, FSum.IsLocal),
          /*NumInsts=*/0, FunctionSummary::FFlags{}, /*EntryCount=*/0, Refs,
          ArrayRef<FunctionSummary::EdgeTy>{}, std::move(FSum.TypeTests),
          std::move(FSum.TypeTestAssumeVCalls),
          std::move(FSum.TypeCheckedLoadVCalls),
          std::move(FSum.TypeTestAssumeConstVCalls),
          std::move(FSum.TypeCheckedLoadConstVCalls)));
    }
  }
  static void output(IO &io, GlobalValueSummaryMapTy &V) {
    for (auto &P : V) {
      std::vector<FunctionSummaryYaml> FSums;
      for (auto &Sum : P.second.SummaryList) {
        if (auto *FSum = dyn_cast<FunctionSummary>(Sum.get())) {
          std::vector<uint64_t> Refs;
          for (auto &VI : FSum->refs())
            Refs.push_back(VI.getGUID());
          FSums.push_back(FunctionSummaryYaml{
              FSum->flags().Linkage,
              static_cast<bool>(FSum->flags().NotEligibleToImport),
              static_cast<bool>(FSum->flags().Live),
              static_cast<bool>(FSum->flags().DSOLocal), Refs,
              FSum->type_tests(), FSum->type_test_assume_vcalls(),
              FSum->type_checked_load_vcalls(),
              FSum->type_test_assume_const_vcalls(),
              FSum->type_checked_load_const_vcalls()});
          }
      }
      if (!FSums.empty())
        io.mapRequired(llvm::utostr(P.first).c_str(), FSums);
    }
  }
};

template <> struct CustomMappingTraits<TypeIdSummaryMapTy> {
  static void inputOne(IO &io, StringRef Key, TypeIdSummaryMapTy &V) {
    TypeIdSummary TId;
    io.mapRequired(Key.str().c_str(), TId);
    V.insert({GlobalValue::getGUID(Key), {Key, TId}});
  }
  static void output(IO &io, TypeIdSummaryMapTy &V) {
    for (auto TidIter = V.begin(); TidIter != V.end(); TidIter++)
      io.mapRequired(TidIter->second.first.c_str(), TidIter->second.second);
  }
};

template <> struct MappingTraits<ModuleSummaryIndex> {
  static void mapping(IO &io, ModuleSummaryIndex& index) {
    io.mapOptional("GlobalValueMap", index.GlobalValueMap);
    io.mapOptional("TypeIdMap", index.TypeIdMap);
    io.mapOptional("WithGlobalValueDeadStripping",
                   index.WithGlobalValueDeadStripping);

    if (io.outputting()) {
      std::vector<std::string> CfiFunctionDefs(index.CfiFunctionDefs.begin(),
                                               index.CfiFunctionDefs.end());
      io.mapOptional("CfiFunctionDefs", CfiFunctionDefs);
      std::vector<std::string> CfiFunctionDecls(index.CfiFunctionDecls.begin(),
                                                index.CfiFunctionDecls.end());
      io.mapOptional("CfiFunctionDecls", CfiFunctionDecls);
    } else {
      std::vector<std::string> CfiFunctionDefs;
      io.mapOptional("CfiFunctionDefs", CfiFunctionDefs);
      index.CfiFunctionDefs = {CfiFunctionDefs.begin(), CfiFunctionDefs.end()};
      std::vector<std::string> CfiFunctionDecls;
      io.mapOptional("CfiFunctionDecls", CfiFunctionDecls);
      index.CfiFunctionDecls = {CfiFunctionDecls.begin(),
                                CfiFunctionDecls.end()};
    }
  }
};

} // End yaml namespace
} // End llvm namespace

#endif
