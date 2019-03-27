//===-- GCNProcessors.td - GCN Processor definitions ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// The code produced for "generic" is only useful for tests and cannot
// reasonably be expected to execute on any particular target.
def : ProcessorModel<"generic", NoSchedModel,
  [FeatureGCN, FeatureWavefrontSize64]
>;

//===----------------------------------------------------------------------===//
// GCN GFX6 (Southern Islands (SI)).
//===----------------------------------------------------------------------===//

def : ProcessorModel<"gfx600", SIFullSpeedModel,
  [FeatureISAVersion6_0_0]
>;

def : ProcessorModel<"tahiti", SIFullSpeedModel,
  [FeatureISAVersion6_0_0]
>;

def : ProcessorModel<"gfx601", SIQuarterSpeedModel,
  [FeatureISAVersion6_0_1]
>;

def : ProcessorModel<"hainan", SIQuarterSpeedModel,
  [FeatureISAVersion6_0_1]
>;

def : ProcessorModel<"oland", SIQuarterSpeedModel,
  [FeatureISAVersion6_0_1]
>;

def : ProcessorModel<"pitcairn", SIQuarterSpeedModel,
  [FeatureISAVersion6_0_1]
>;

def : ProcessorModel<"verde", SIQuarterSpeedModel,
  [FeatureISAVersion6_0_1]
>;

//===----------------------------------------------------------------------===//
// GCN GFX7 (Sea Islands (CI)).
//===----------------------------------------------------------------------===//

def : ProcessorModel<"gfx700", SIQuarterSpeedModel,
  [FeatureISAVersion7_0_0]
>;

def : ProcessorModel<"kaveri", SIQuarterSpeedModel,
  [FeatureISAVersion7_0_0]
>;

def : ProcessorModel<"gfx701", SIFullSpeedModel,
  [FeatureISAVersion7_0_1]
>;

def : ProcessorModel<"hawaii", SIFullSpeedModel,
  [FeatureISAVersion7_0_1]
>;

def : ProcessorModel<"gfx702", SIQuarterSpeedModel,
  [FeatureISAVersion7_0_2]
>;

def : ProcessorModel<"gfx703", SIQuarterSpeedModel,
  [FeatureISAVersion7_0_3]
>;

def : ProcessorModel<"kabini", SIQuarterSpeedModel,
  [FeatureISAVersion7_0_3]
>;

def : ProcessorModel<"mullins", SIQuarterSpeedModel,
  [FeatureISAVersion7_0_3]
>;

def : ProcessorModel<"gfx704", SIQuarterSpeedModel,
  [FeatureISAVersion7_0_4]
>;

def : ProcessorModel<"bonaire", SIQuarterSpeedModel,
  [FeatureISAVersion7_0_4]
>;

//===----------------------------------------------------------------------===//
// GCN GFX8 (Volcanic Islands (VI)).
//===----------------------------------------------------------------------===//

def : ProcessorModel<"gfx801", SIQuarterSpeedModel,
  [FeatureISAVersion8_0_1]
>;

def : ProcessorModel<"carrizo", SIQuarterSpeedModel,
  [FeatureISAVersion8_0_1]
>;

def : ProcessorModel<"gfx802", SIQuarterSpeedModel,
  [FeatureISAVersion8_0_2]
>;

def : ProcessorModel<"iceland", SIQuarterSpeedModel,
  [FeatureISAVersion8_0_2]
>;

def : ProcessorModel<"tonga", SIQuarterSpeedModel,
  [FeatureISAVersion8_0_2]
>;

def : ProcessorModel<"gfx803", SIQuarterSpeedModel,
  [FeatureISAVersion8_0_3]
>;

def : ProcessorModel<"fiji", SIQuarterSpeedModel,
  [FeatureISAVersion8_0_3]
>;

def : ProcessorModel<"polaris10", SIQuarterSpeedModel,
  [FeatureISAVersion8_0_3]
>;

def : ProcessorModel<"polaris11", SIQuarterSpeedModel,
  [FeatureISAVersion8_0_3]
>;

def : ProcessorModel<"gfx810", SIQuarterSpeedModel,
  [FeatureISAVersion8_1_0]
>;

def : ProcessorModel<"stoney", SIQuarterSpeedModel,
  [FeatureISAVersion8_1_0]
>;

//===----------------------------------------------------------------------===//
// GCN GFX9.
//===----------------------------------------------------------------------===//

def : ProcessorModel<"gfx900", SIQuarterSpeedModel,
  [FeatureISAVersion9_0_0]
>;

def : ProcessorModel<"gfx902", SIQuarterSpeedModel,
  [FeatureISAVersion9_0_2]
>;

def : ProcessorModel<"gfx904", SIQuarterSpeedModel,
  [FeatureISAVersion9_0_4]
>;

def : ProcessorModel<"gfx906", SIQuarterSpeedModel,
  [FeatureISAVersion9_0_6]
>;

def : ProcessorModel<"gfx909", SIQuarterSpeedModel,
  [FeatureISAVersion9_0_9]
>;

