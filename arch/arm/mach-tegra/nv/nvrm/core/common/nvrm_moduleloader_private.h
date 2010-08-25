/*
 * arch/arm/mach-tegra/nvrm/core/common/nvrm_moduleloader_private.h
 *
 * AVP firmware module loader
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef INCLUDED_NVRM_MODULELOADER_PRIVATE_H
#define INCLUDED_NVRM_MODULELOADER_PRIVATE_H

#include "nvrm_moduleloader.h"
#include "nvrm_memmgr.h"

typedef struct PrivateOsFileRec
{
    const NvU8 *pstart;
    const NvU8 *pread;
    const NvU8 *pend;
} PrivateOsFile;

typedef struct PrivateOsFileRec *PrivateOsFileHandle;

#define LOAD_ADDRESS        0x11001000
#define IRAM_PREF_EXT_ADDRESS   0x50000000
#define IRAM_MAND_ADDRESS   0x40000000
#define DRAM_MAND_ADDRESS   0x10000000
#define DT_ARM_SYMTABSZ     0x70000001
#define DT_ARM_RESERVED1    0x70000000

/// ELF magic number
enum
{
    ELF_MAG0 = 0x7F
};

/// ELF section header entry types.
enum
{
    SHT_INIT_ARRAY = 12,             ///< Code initialization array
    SHT_FINI_ARRAY,             ///< Code finalization array
    SHT_PREINIT_ARRAY,          ///< Code pre-inialization array
    SHT_GROUP,                  ///< Group
    SHT_SYMTAB_SHNDX,           ///< Symbol table index
};
#define SHT_LOPROC 0x70000000    ///< Start of processor-specific
#define SHT_HIPROC 0x7fffffff    ///< End of processor-specific
#define SHT_LOUSER 0x80000000    ///< Start of application-specific
#define SHT_HIUSER 0xffffffff     ///< End of application-specific

/// ELF dynamic section type flags
enum
{
    DT_NUM              = 34,           ///< Number used
};

/// ARM specific relocation codes
enum
{
    R_ARM_RABS32 = 253,
};

/// A linked list of load segment records
typedef struct SegmentRec SegmentNode;

struct SegmentRec
{
    NvRmMemHandle pLoadRegion;
    NvU32 LoadAddress;
    NvU32 Index;
    NvU32 VirtualAddr;
    NvU32 MemorySize;
    NvU32 FileOffset;
    NvU32 FileSize;
    void* MapAddr;
    SegmentNode *Next;
};

/// ModuleLoader handle structure
typedef struct NvRmLibraryRec
{
    NvU32 libraryId;
} NvRmLibHandle;

NvError
NvRmPrivLoadKernelLibrary(NvRmDeviceHandle hDevice,
                      const char *pLibName,
                      NvRmLibraryHandle *hLibHandle);

/// Add a load region to the segment list
SegmentNode* AddToSegmentList(SegmentNode *pList,
                      NvRmMemHandle pRegion,
                      Elf32_Phdr Phdr,
                      NvU32 Idx,
                      NvU32 PhysAddr,
                      void* MapAddr);

/// Apply the relocation code based on relocation info from relocation table
NvError
ApplyRelocation(SegmentNode *pList,
                NvU32 FileOffset,
                NvU32 SegmentOffset,
                NvRmMemHandle pRegion,
                const Elf32_Rel *pRel);

/// Get the special section name for a given section type and flag
NvError
GetSpecialSectionName(Elf32_Word SectionType,
                      Elf32_Word SectionFlags,
                      const char** SpecialSectionName);

/// Parse the dynamic segment of ELF to extract the relocation table
 NvError
ParseDynamicSegment(SegmentNode *pList,
                    const char* pSegmentData,
                    size_t SegmentSize,
                    NvU32 DynamicSegmentOffset);

/// Parse ELF library and load the relocated library segments for a given library name
NvError NvRmPrivLoadLibrary(NvRmDeviceHandle hDevice,
                                                            const char *Filename,
                                                            NvU32 Address,
                                                            NvBool IsApproachGreedy,
                                                            NvRmLibraryHandle *hLibHandle);

/// Get the symbol address. In phase1, this api will return the entry point address of the module
NvError
NvRmPrivGetProcAddress(NvRmLibraryHandle Handle,
               const char *pSymbol,
               void **pSymAddress);
/// Free the ELF library by unloading the library from memory
NvError NvRmPrivFreeLibrary(NvRmLibHandle *hLibHandle);

NvError NvRmPrivInitModuleLoaderRPC(NvRmDeviceHandle hDevice);

/// Unmap memory segments
void UnMapRegion(SegmentNode *pList);
/// Unload segments
void RemoveRegion(SegmentNode *pList);

void parseElfHeader(Elf32_Ehdr *elf);

NvError
LoadLoadableProgramSegment(PrivateOsFileHandle elfSourceHandle,
            NvRmDeviceHandle hDevice,
            NvRmLibraryHandle hLibHandle,
            Elf32_Phdr Phdr,
            Elf32_Ehdr Ehdr,
            const NvRmHeap * Heaps,
            NvU32 NumHeaps,
            NvU32 loop,
            const char *Filename,
            SegmentNode **segmentList);

NvError
parseProgramSegmentHeaders(PrivateOsFileHandle elfSourceHandle,
            NvU32 segmentHeaderOffset,
            NvU32 segmentCount);

 NvError
parseSectionHeaders(PrivateOsFileHandle elfSourceHandle,
            Elf32_Ehdr *elf);

NvError
loadSegmentsInFixedMemory(PrivateOsFileHandle elfSourceHandle,
                        Elf32_Ehdr *elf, NvU32 segmentIndex, void **loadaddress);
#endif
