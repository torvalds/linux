//===- xray-fdr-dump.cpp: XRay FDR Trace Dump Tool ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implements the FDR trace dumping tool, using the libraries for handling FDR
// mode traces specifically.
//
//===----------------------------------------------------------------------===//
#include "xray-registry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/XRay/BlockIndexer.h"
#include "llvm/XRay/BlockPrinter.h"
#include "llvm/XRay/BlockVerifier.h"
#include "llvm/XRay/FDRRecordConsumer.h"
#include "llvm/XRay/FDRRecordProducer.h"
#include "llvm/XRay/FDRRecords.h"
#include "llvm/XRay/FileHeaderReader.h"
#include "llvm/XRay/RecordPrinter.h"

using namespace llvm;
using namespace xray;

static cl::SubCommand Dump("fdr-dump", "FDR Trace Dump");
static cl::opt<std::string> DumpInput(cl::Positional,
                                      cl::desc("<xray fdr mode log>"),
                                      cl::Required, cl::sub(Dump));
static cl::opt<bool> DumpVerify("verify",
                                cl::desc("verify structure of the log"),
                                cl::init(false), cl::sub(Dump));

static CommandRegistration Unused(&Dump, []() -> Error {
  // Open the file provided.
  int Fd;
  if (auto EC = sys::fs::openFileForRead(DumpInput, Fd))
    return createStringError(EC, "Cannot open file '%s' for read.",
                             DumpInput.c_str());

  uint64_t FileSize;
  if (auto EC = sys::fs::file_size(DumpInput, FileSize))
    return createStringError(EC, "Failed to get file size for '%s'.",
                             DumpInput.c_str());

  std::error_code EC;
  sys::fs::mapped_file_region MappedFile(
      Fd, sys::fs::mapped_file_region::mapmode::readonly, FileSize, 0, EC);

  DataExtractor DE(StringRef(MappedFile.data(), MappedFile.size()), true, 8);
  uint32_t OffsetPtr = 0;

  auto FileHeaderOrError = readBinaryFormatHeader(DE, OffsetPtr);
  if (!FileHeaderOrError)
    return FileHeaderOrError.takeError();
  auto &H = FileHeaderOrError.get();

  FileBasedRecordProducer P(H, DE, OffsetPtr);

  RecordPrinter RP(outs(), "\n");
  if (!DumpVerify) {
    PipelineConsumer C({&RP});
    while (DE.isValidOffsetForDataOfSize(OffsetPtr, 1)) {
      auto R = P.produce();
      if (!R)
        return R.takeError();
      if (auto E = C.consume(std::move(R.get())))
        return E;
    }
    return Error::success();
  }

  BlockPrinter BP(outs(), RP);
  std::vector<std::unique_ptr<Record>> Records;
  LogBuilderConsumer C(Records);
  while (DE.isValidOffsetForDataOfSize(OffsetPtr, 1)) {
    auto R = P.produce();
    if (!R) {
      // Print records we've found so far.
      for (auto &Ptr : Records)
        if (auto E = Ptr->apply(RP))
          return joinErrors(std::move(E), R.takeError());
      return R.takeError();
    }
    if (auto E = C.consume(std::move(R.get())))
      return E;
  }

  // Once we have a trace, we then index the blocks.
  BlockIndexer::Index Index;
  BlockIndexer BI(Index);
  for (auto &Ptr : Records)
    if (auto E = Ptr->apply(BI))
      return E;

  if (auto E = BI.flush())
    return E;

  // Then we validate while printing each block.
  BlockVerifier BV;
  for (auto ProcessThreadBlocks : Index) {
    auto &Blocks = ProcessThreadBlocks.second;
    for (auto &B : Blocks) {
      for (auto *R : B.Records) {
        if (auto E = R->apply(BV))
          return E;
        if (auto E = R->apply(BP))
          return E;
      }
      BV.reset();
      BP.reset();
    }
  }
  outs().flush();
  return Error::success();
});
