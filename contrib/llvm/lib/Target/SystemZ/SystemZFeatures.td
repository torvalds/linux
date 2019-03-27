//===-- SystemZ.td - SystemZ processors and features ---------*- tblgen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Feature definitions.
//
//===----------------------------------------------------------------------===//

class SystemZFeature<string extname, string intname, string desc>
  : Predicate<"Subtarget->has"##intname##"()">,
    AssemblerPredicate<"Feature"##intname, extname>,
    SubtargetFeature<extname, "Has"##intname, "true", desc>;

class SystemZMissingFeature<string intname>
  : Predicate<"!Subtarget->has"##intname##"()">;

class SystemZFeatureList<list<SystemZFeature> x> {
  list<SystemZFeature> List = x;
}

class SystemZFeatureAdd<list<SystemZFeature> x, list<SystemZFeature> y>
  : SystemZFeatureList<!listconcat(x, y)>;

//===----------------------------------------------------------------------===//
//
// New features added in the Ninth Edition of the z/Architecture
//
//===----------------------------------------------------------------------===//

def FeatureDistinctOps : SystemZFeature<
  "distinct-ops", "DistinctOps",
  "Assume that the distinct-operands facility is installed"
>;

def FeatureFastSerialization : SystemZFeature<
  "fast-serialization", "FastSerialization",
  "Assume that the fast-serialization facility is installed"
>;

def FeatureFPExtension : SystemZFeature<
  "fp-extension", "FPExtension",
  "Assume that the floating-point extension facility is installed"
>;

def FeatureHighWord : SystemZFeature<
  "high-word", "HighWord",
  "Assume that the high-word facility is installed"
>;

def FeatureInterlockedAccess1 : SystemZFeature<
  "interlocked-access1", "InterlockedAccess1",
  "Assume that interlocked-access facility 1 is installed"
>;
def FeatureNoInterlockedAccess1 : SystemZMissingFeature<"InterlockedAccess1">;

def FeatureLoadStoreOnCond : SystemZFeature<
  "load-store-on-cond", "LoadStoreOnCond",
  "Assume that the load/store-on-condition facility is installed"
>;
def FeatureNoLoadStoreOnCond : SystemZMissingFeature<"LoadStoreOnCond">;

def FeaturePopulationCount : SystemZFeature<
  "population-count", "PopulationCount",
  "Assume that the population-count facility is installed"
>;

def FeatureMessageSecurityAssist3 : SystemZFeature<
  "message-security-assist-extension3", "MessageSecurityAssist3",
  "Assume that the message-security-assist extension facility 3 is installed"
>;

def FeatureMessageSecurityAssist4 : SystemZFeature<
  "message-security-assist-extension4", "MessageSecurityAssist4",
  "Assume that the message-security-assist extension facility 4 is installed"
>;

def FeatureResetReferenceBitsMultiple : SystemZFeature<
  "reset-reference-bits-multiple", "ResetReferenceBitsMultiple",
  "Assume that the reset-reference-bits-multiple facility is installed"
>;

def Arch9NewFeatures : SystemZFeatureList<[
    FeatureDistinctOps,
    FeatureFastSerialization,
    FeatureFPExtension,
    FeatureHighWord,
    FeatureInterlockedAccess1,
    FeatureLoadStoreOnCond,
    FeaturePopulationCount,
    FeatureMessageSecurityAssist3,
    FeatureMessageSecurityAssist4,
    FeatureResetReferenceBitsMultiple
]>;

//===----------------------------------------------------------------------===//
//
// New features added in the Tenth Edition of the z/Architecture
//
//===----------------------------------------------------------------------===//

def FeatureExecutionHint : SystemZFeature<
  "execution-hint", "ExecutionHint",
  "Assume that the execution-hint facility is installed"
>;

def FeatureLoadAndTrap : SystemZFeature<
  "load-and-trap", "LoadAndTrap",
  "Assume that the load-and-trap facility is installed"
>;

def FeatureMiscellaneousExtensions : SystemZFeature<
  "miscellaneous-extensions", "MiscellaneousExtensions",
  "Assume that the miscellaneous-extensions facility is installed"
>;

def FeatureProcessorAssist : SystemZFeature<
  "processor-assist", "ProcessorAssist",
  "Assume that the processor-assist facility is installed"
>;

def FeatureTransactionalExecution : SystemZFeature<
  "transactional-execution", "TransactionalExecution",
  "Assume that the transactional-execution facility is installed"
>;

def FeatureDFPZonedConversion : SystemZFeature<
  "dfp-zoned-conversion", "DFPZonedConversion",
  "Assume that the DFP zoned-conversion facility is installed"
>;

def FeatureEnhancedDAT2 : SystemZFeature<
  "enhanced-dat-2", "EnhancedDAT2",
  "Assume that the enhanced-DAT facility 2 is installed"
>;

def Arch10NewFeatures : SystemZFeatureList<[
    FeatureExecutionHint,
    FeatureLoadAndTrap,
    FeatureMiscellaneousExtensions,
    FeatureProcessorAssist,
    FeatureTransactionalExecution,
    FeatureDFPZonedConversion,
    FeatureEnhancedDAT2
]>;

//===----------------------------------------------------------------------===//
//
// New features added in the Eleventh Edition of the z/Architecture
//
//===----------------------------------------------------------------------===//

def FeatureLoadAndZeroRightmostByte : SystemZFeature<
  "load-and-zero-rightmost-byte", "LoadAndZeroRightmostByte",
  "Assume that the load-and-zero-rightmost-byte facility is installed"
>;

def FeatureLoadStoreOnCond2 : SystemZFeature<
  "load-store-on-cond-2", "LoadStoreOnCond2",
  "Assume that the load/store-on-condition facility 2 is installed"
>;

def FeatureMessageSecurityAssist5 : SystemZFeature<
  "message-security-assist-extension5", "MessageSecurityAssist5",
  "Assume that the message-security-assist extension facility 5 is installed"
>;

def FeatureDFPPackedConversion : SystemZFeature<
  "dfp-packed-conversion", "DFPPackedConversion",
  "Assume that the DFP packed-conversion facility is installed"
>;

def FeatureVector : SystemZFeature<
  "vector", "Vector",
  "Assume that the vectory facility is installed"
>;
def FeatureNoVector : SystemZMissingFeature<"Vector">;

def Arch11NewFeatures : SystemZFeatureList<[
    FeatureLoadAndZeroRightmostByte,
    FeatureLoadStoreOnCond2,
    FeatureMessageSecurityAssist5,
    FeatureDFPPackedConversion,
    FeatureVector
]>;

//===----------------------------------------------------------------------===//
//
// New features added in the Twelvth Edition of the z/Architecture
//
//===----------------------------------------------------------------------===//

def FeatureMiscellaneousExtensions2 : SystemZFeature<
  "miscellaneous-extensions-2", "MiscellaneousExtensions2",
  "Assume that the miscellaneous-extensions facility 2 is installed"
>;

def FeatureGuardedStorage : SystemZFeature<
  "guarded-storage", "GuardedStorage",
  "Assume that the guarded-storage facility is installed"
>;

def FeatureMessageSecurityAssist7 : SystemZFeature<
  "message-security-assist-extension7", "MessageSecurityAssist7",
  "Assume that the message-security-assist extension facility 7 is installed"
>;

def FeatureMessageSecurityAssist8 : SystemZFeature<
  "message-security-assist-extension8", "MessageSecurityAssist8",
  "Assume that the message-security-assist extension facility 8 is installed"
>;

def FeatureVectorEnhancements1 : SystemZFeature<
  "vector-enhancements-1", "VectorEnhancements1",
  "Assume that the vector enhancements facility 1 is installed"
>;
def FeatureNoVectorEnhancements1 : SystemZMissingFeature<"VectorEnhancements1">;

def FeatureVectorPackedDecimal : SystemZFeature<
  "vector-packed-decimal", "VectorPackedDecimal",
  "Assume that the vector packed decimal facility is installed"
>;

def FeatureInsertReferenceBitsMultiple : SystemZFeature<
  "insert-reference-bits-multiple", "InsertReferenceBitsMultiple",
  "Assume that the insert-reference-bits-multiple facility is installed"
>;

def Arch12NewFeatures : SystemZFeatureList<[
    FeatureMiscellaneousExtensions2,
    FeatureGuardedStorage,
    FeatureMessageSecurityAssist7,
    FeatureMessageSecurityAssist8,
    FeatureVectorEnhancements1,
    FeatureVectorPackedDecimal,
    FeatureInsertReferenceBitsMultiple
]>;

//===----------------------------------------------------------------------===//
//
// Cumulative supported and unsupported feature sets
//
//===----------------------------------------------------------------------===//

def Arch8SupportedFeatures
  : SystemZFeatureList<[]>;
def Arch9SupportedFeatures
  : SystemZFeatureAdd<Arch8SupportedFeatures.List,  Arch9NewFeatures.List>;
def Arch10SupportedFeatures
  : SystemZFeatureAdd<Arch9SupportedFeatures.List,  Arch10NewFeatures.List>;
def Arch11SupportedFeatures
  : SystemZFeatureAdd<Arch10SupportedFeatures.List, Arch11NewFeatures.List>;
def Arch12SupportedFeatures
  : SystemZFeatureAdd<Arch11SupportedFeatures.List, Arch12NewFeatures.List>;

def Arch12UnsupportedFeatures
  : SystemZFeatureList<[]>;
def Arch11UnsupportedFeatures
  : SystemZFeatureAdd<Arch12UnsupportedFeatures.List, Arch12NewFeatures.List>;
def Arch10UnsupportedFeatures
  : SystemZFeatureAdd<Arch11UnsupportedFeatures.List, Arch11NewFeatures.List>;
def Arch9UnsupportedFeatures
  : SystemZFeatureAdd<Arch10UnsupportedFeatures.List, Arch10NewFeatures.List>;
def Arch8UnsupportedFeatures
  : SystemZFeatureAdd<Arch9UnsupportedFeatures.List,  Arch9NewFeatures.List>;

