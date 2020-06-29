/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_INTEL_FAMILY_H
#define _ASM_X86_INTEL_FAMILY_H

/*
 * "Big Core" Processors (Branded as Core, Xeon, etc...)
 *
 * While adding a new CPUID for a new microarchitecture, add a new
 * group to keep logically sorted out in chronological order. Within
 * that group keep the CPUID for the variants sorted by model number.
 *
 * The defined symbol names have the following form:
 *	INTEL_FAM6{OPTFAMILY}_{MICROARCH}{OPTDIFF}
 * where:
 * OPTFAMILY	Describes the family of CPUs that this belongs to. Default
 *		is assumed to be "_CORE" (and should be omitted). Other values
 *		currently in use are _ATOM and _XEON_PHI
 * MICROARCH	Is the code name for the micro-architecture for this core.
 *		N.B. Not the platform name.
 * OPTDIFF	If needed, a short string to differentiate by market segment.
 *
 *		Common OPTDIFFs:
 *
 *			- regular client parts
 *		_L	- regular mobile parts
 *		_G	- parts with extra graphics on
 *		_X	- regular server parts
 *		_D	- micro server parts
 *
 *		Historical OPTDIFFs:
 *
 *		_EP	- 2 socket server parts
 *		_EX	- 4+ socket server parts
 *
 * The #define line may optionally include a comment including platform names.
 */

/* Wildcard match for FAM6 so X86_MATCH_INTEL_FAM6_MODEL(ANY) works */
#define INTEL_FAM6_ANY			X86_MODEL_ANY

#define INTEL_FAM6_CORE_YONAH		0x0E

#define INTEL_FAM6_CORE2_MEROM		0x0F
#define INTEL_FAM6_CORE2_MEROM_L	0x16
#define INTEL_FAM6_CORE2_PENRYN		0x17
#define INTEL_FAM6_CORE2_DUNNINGTON	0x1D

#define INTEL_FAM6_NEHALEM		0x1E
#define INTEL_FAM6_NEHALEM_G		0x1F /* Auburndale / Havendale */
#define INTEL_FAM6_NEHALEM_EP		0x1A
#define INTEL_FAM6_NEHALEM_EX		0x2E

#define INTEL_FAM6_WESTMERE		0x25
#define INTEL_FAM6_WESTMERE_EP		0x2C
#define INTEL_FAM6_WESTMERE_EX		0x2F

#define INTEL_FAM6_SANDYBRIDGE		0x2A
#define INTEL_FAM6_SANDYBRIDGE_X	0x2D
#define INTEL_FAM6_IVYBRIDGE		0x3A
#define INTEL_FAM6_IVYBRIDGE_X		0x3E

#define INTEL_FAM6_HASWELL		0x3C
#define INTEL_FAM6_HASWELL_X		0x3F
#define INTEL_FAM6_HASWELL_L		0x45
#define INTEL_FAM6_HASWELL_G		0x46

#define INTEL_FAM6_BROADWELL		0x3D
#define INTEL_FAM6_BROADWELL_G		0x47
#define INTEL_FAM6_BROADWELL_X		0x4F
#define INTEL_FAM6_BROADWELL_D		0x56

#define INTEL_FAM6_SKYLAKE_L		0x4E
#define INTEL_FAM6_SKYLAKE		0x5E
#define INTEL_FAM6_SKYLAKE_X		0x55
#define INTEL_FAM6_KABYLAKE_L		0x8E
#define INTEL_FAM6_KABYLAKE		0x9E

#define INTEL_FAM6_CANNONLAKE_L		0x66

#define INTEL_FAM6_ICELAKE_X		0x6A
#define INTEL_FAM6_ICELAKE_D		0x6C
#define INTEL_FAM6_ICELAKE		0x7D
#define INTEL_FAM6_ICELAKE_L		0x7E
#define INTEL_FAM6_ICELAKE_NNPI		0x9D

#define INTEL_FAM6_TIGERLAKE_L		0x8C
#define INTEL_FAM6_TIGERLAKE		0x8D

#define INTEL_FAM6_COMETLAKE		0xA5
#define INTEL_FAM6_COMETLAKE_L		0xA6

#define INTEL_FAM6_SAPPHIRERAPIDS_X	0x8F

/* "Small Core" Processors (Atom) */

#define INTEL_FAM6_ATOM_BONNELL		0x1C /* Diamondville, Pineview */
#define INTEL_FAM6_ATOM_BONNELL_MID	0x26 /* Silverthorne, Lincroft */

#define INTEL_FAM6_ATOM_SALTWELL	0x36 /* Cedarview */
#define INTEL_FAM6_ATOM_SALTWELL_MID	0x27 /* Penwell */
#define INTEL_FAM6_ATOM_SALTWELL_TABLET	0x35 /* Cloverview */

#define INTEL_FAM6_ATOM_SILVERMONT	0x37 /* Bay Trail, Valleyview */
#define INTEL_FAM6_ATOM_SILVERMONT_D	0x4D /* Avaton, Rangely */
#define INTEL_FAM6_ATOM_SILVERMONT_MID	0x4A /* Merriefield */

#define INTEL_FAM6_ATOM_AIRMONT		0x4C /* Cherry Trail, Braswell */
#define INTEL_FAM6_ATOM_AIRMONT_MID	0x5A /* Moorefield */
#define INTEL_FAM6_ATOM_AIRMONT_NP	0x75 /* Lightning Mountain */

#define INTEL_FAM6_ATOM_GOLDMONT	0x5C /* Apollo Lake */
#define INTEL_FAM6_ATOM_GOLDMONT_D	0x5F /* Denverton */

/* Note: the micro-architecture is "Goldmont Plus" */
#define INTEL_FAM6_ATOM_GOLDMONT_PLUS	0x7A /* Gemini Lake */

#define INTEL_FAM6_ATOM_TREMONT_D	0x86 /* Jacobsville */
#define INTEL_FAM6_ATOM_TREMONT		0x96 /* Elkhart Lake */
#define INTEL_FAM6_ATOM_TREMONT_L	0x9C /* Jasper Lake */

/* Xeon Phi */

#define INTEL_FAM6_XEON_PHI_KNL		0x57 /* Knights Landing */
#define INTEL_FAM6_XEON_PHI_KNM		0x85 /* Knights Mill */

/* Family 5 */
#define INTEL_FAM5_QUARK_X1000		0x09 /* Quark X1000 SoC */

#endif /* _ASM_X86_INTEL_FAMILY_H */
