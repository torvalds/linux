//===------------------ mach-o/compact_unwind_encoding.h ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//
// Darwin's alternative to DWARF based unwind encodings.
//
//===----------------------------------------------------------------------===//


#ifndef __COMPACT_UNWIND_ENCODING__
#define __COMPACT_UNWIND_ENCODING__

#include <stdint.h>

//
// Compilers can emit standard DWARF FDEs in the __TEXT,__eh_frame section
// of object files. Or compilers can emit compact unwind information in
// the __LD,__compact_unwind section.
//
// When the linker creates a final linked image, it will create a
// __TEXT,__unwind_info section.  This section is a small and fast way for the
// runtime to access unwind info for any given function.  If the compiler
// emitted compact unwind info for the function, that compact unwind info will
// be encoded in the __TEXT,__unwind_info section. If the compiler emitted
// DWARF unwind info, the __TEXT,__unwind_info section will contain the offset
// of the FDE in the __TEXT,__eh_frame section in the final linked image.
//
// Note: Previously, the linker would transform some DWARF unwind infos into
//       compact unwind info.  But that is fragile and no longer done.


//
// The compact unwind endoding is a 32-bit value which encoded in an
// architecture specific way, which registers to restore from where, and how
// to unwind out of the function.
//
typedef uint32_t compact_unwind_encoding_t;


// architecture independent bits
enum {
    UNWIND_IS_NOT_FUNCTION_START           = 0x80000000,
    UNWIND_HAS_LSDA                        = 0x40000000,
    UNWIND_PERSONALITY_MASK                = 0x30000000,
};




//
// x86
//
// 1-bit: start
// 1-bit: has lsda
// 2-bit: personality index
//
// 4-bits: 0=old, 1=ebp based, 2=stack-imm, 3=stack-ind, 4=DWARF
//  ebp based:
//        15-bits (5*3-bits per reg) register permutation
//        8-bits for stack offset
//  frameless:
//        8-bits stack size
//        3-bits stack adjust
//        3-bits register count
//        10-bits register permutation
//
enum {
    UNWIND_X86_MODE_MASK                         = 0x0F000000,
    UNWIND_X86_MODE_EBP_FRAME                    = 0x01000000,
    UNWIND_X86_MODE_STACK_IMMD                   = 0x02000000,
    UNWIND_X86_MODE_STACK_IND                    = 0x03000000,
    UNWIND_X86_MODE_DWARF                        = 0x04000000,

    UNWIND_X86_EBP_FRAME_REGISTERS               = 0x00007FFF,
    UNWIND_X86_EBP_FRAME_OFFSET                  = 0x00FF0000,

    UNWIND_X86_FRAMELESS_STACK_SIZE              = 0x00FF0000,
    UNWIND_X86_FRAMELESS_STACK_ADJUST            = 0x0000E000,
    UNWIND_X86_FRAMELESS_STACK_REG_COUNT         = 0x00001C00,
    UNWIND_X86_FRAMELESS_STACK_REG_PERMUTATION   = 0x000003FF,

    UNWIND_X86_DWARF_SECTION_OFFSET              = 0x00FFFFFF,
};

enum {
    UNWIND_X86_REG_NONE     = 0,
    UNWIND_X86_REG_EBX      = 1,
    UNWIND_X86_REG_ECX      = 2,
    UNWIND_X86_REG_EDX      = 3,
    UNWIND_X86_REG_EDI      = 4,
    UNWIND_X86_REG_ESI      = 5,
    UNWIND_X86_REG_EBP      = 6,
};

//
// For x86 there are four modes for the compact unwind encoding:
// UNWIND_X86_MODE_EBP_FRAME:
//    EBP based frame where EBP is push on stack immediately after return address,
//    then ESP is moved to EBP. Thus, to unwind ESP is restored with the current
//    EPB value, then EBP is restored by popping off the stack, and the return
//    is done by popping the stack once more into the pc.
//    All non-volatile registers that need to be restored must have been saved
//    in a small range in the stack that starts EBP-4 to EBP-1020.  The offset/4
//    is encoded in the UNWIND_X86_EBP_FRAME_OFFSET bits.  The registers saved
//    are encoded in the UNWIND_X86_EBP_FRAME_REGISTERS bits as five 3-bit entries.
//    Each entry contains which register to restore.
// UNWIND_X86_MODE_STACK_IMMD:
//    A "frameless" (EBP not used as frame pointer) function with a small 
//    constant stack size.  To return, a constant (encoded in the compact
//    unwind encoding) is added to the ESP. Then the return is done by
//    popping the stack into the pc.
//    All non-volatile registers that need to be restored must have been saved
//    on the stack immediately after the return address.  The stack_size/4 is
//    encoded in the UNWIND_X86_FRAMELESS_STACK_SIZE (max stack size is 1024).
//    The number of registers saved is encoded in UNWIND_X86_FRAMELESS_STACK_REG_COUNT.
//    UNWIND_X86_FRAMELESS_STACK_REG_PERMUTATION constains which registers were
//    saved and their order.
// UNWIND_X86_MODE_STACK_IND:
//    A "frameless" (EBP not used as frame pointer) function large constant 
//    stack size.  This case is like the previous, except the stack size is too
//    large to encode in the compact unwind encoding.  Instead it requires that 
//    the function contains "subl $nnnnnnnn,ESP" in its prolog.  The compact 
//    encoding contains the offset to the nnnnnnnn value in the function in
//    UNWIND_X86_FRAMELESS_STACK_SIZE.  
// UNWIND_X86_MODE_DWARF:
//    No compact unwind encoding is available.  Instead the low 24-bits of the
//    compact encoding is the offset of the DWARF FDE in the __eh_frame section.
//    This mode is never used in object files.  It is only generated by the 
//    linker in final linked images which have only DWARF unwind info for a
//    function.
//
// The permutation encoding is a Lehmer code sequence encoded into a
// single variable-base number so we can encode the ordering of up to
// six registers in a 10-bit space.
//
// The following is the algorithm used to create the permutation encoding used
// with frameless stacks.  It is passed the number of registers to be saved and
// an array of the register numbers saved.
//
//uint32_t permute_encode(uint32_t registerCount, const uint32_t registers[6])
//{
//    uint32_t renumregs[6];
//    for (int i=6-registerCount; i < 6; ++i) {
//        int countless = 0;
//        for (int j=6-registerCount; j < i; ++j) {
//            if ( registers[j] < registers[i] )
//                ++countless;
//        }
//        renumregs[i] = registers[i] - countless -1;
//    }
//    uint32_t permutationEncoding = 0;
//    switch ( registerCount ) {
//        case 6:
//            permutationEncoding |= (120*renumregs[0] + 24*renumregs[1]
//                                    + 6*renumregs[2] + 2*renumregs[3]
//                                      + renumregs[4]);
//            break;
//        case 5:
//            permutationEncoding |= (120*renumregs[1] + 24*renumregs[2]
//                                    + 6*renumregs[3] + 2*renumregs[4]
//                                      + renumregs[5]);
//            break;
//        case 4:
//            permutationEncoding |= (60*renumregs[2] + 12*renumregs[3]
//                                   + 3*renumregs[4] + renumregs[5]);
//            break;
//        case 3:
//            permutationEncoding |= (20*renumregs[3] + 4*renumregs[4]
//                                     + renumregs[5]);
//            break;
//        case 2:
//            permutationEncoding |= (5*renumregs[4] + renumregs[5]);
//            break;
//        case 1:
//            permutationEncoding |= (renumregs[5]);
//            break;
//    }
//    return permutationEncoding;
//}
//




//
// x86_64
//
// 1-bit: start
// 1-bit: has lsda
// 2-bit: personality index
//
// 4-bits: 0=old, 1=rbp based, 2=stack-imm, 3=stack-ind, 4=DWARF
//  rbp based:
//        15-bits (5*3-bits per reg) register permutation
//        8-bits for stack offset
//  frameless:
//        8-bits stack size
//        3-bits stack adjust
//        3-bits register count
//        10-bits register permutation
//
enum {
    UNWIND_X86_64_MODE_MASK                         = 0x0F000000,
    UNWIND_X86_64_MODE_RBP_FRAME                    = 0x01000000,
    UNWIND_X86_64_MODE_STACK_IMMD                   = 0x02000000,
    UNWIND_X86_64_MODE_STACK_IND                    = 0x03000000,
    UNWIND_X86_64_MODE_DWARF                        = 0x04000000,

    UNWIND_X86_64_RBP_FRAME_REGISTERS               = 0x00007FFF,
    UNWIND_X86_64_RBP_FRAME_OFFSET                  = 0x00FF0000,

    UNWIND_X86_64_FRAMELESS_STACK_SIZE              = 0x00FF0000,
    UNWIND_X86_64_FRAMELESS_STACK_ADJUST            = 0x0000E000,
    UNWIND_X86_64_FRAMELESS_STACK_REG_COUNT         = 0x00001C00,
    UNWIND_X86_64_FRAMELESS_STACK_REG_PERMUTATION   = 0x000003FF,

    UNWIND_X86_64_DWARF_SECTION_OFFSET              = 0x00FFFFFF,
};

enum {
    UNWIND_X86_64_REG_NONE       = 0,
    UNWIND_X86_64_REG_RBX        = 1,
    UNWIND_X86_64_REG_R12        = 2,
    UNWIND_X86_64_REG_R13        = 3,
    UNWIND_X86_64_REG_R14        = 4,
    UNWIND_X86_64_REG_R15        = 5,
    UNWIND_X86_64_REG_RBP        = 6,
};
//
// For x86_64 there are four modes for the compact unwind encoding:
// UNWIND_X86_64_MODE_RBP_FRAME:
//    RBP based frame where RBP is push on stack immediately after return address,
//    then RSP is moved to RBP. Thus, to unwind RSP is restored with the current 
//    EPB value, then RBP is restored by popping off the stack, and the return 
//    is done by popping the stack once more into the pc.
//    All non-volatile registers that need to be restored must have been saved
//    in a small range in the stack that starts RBP-8 to RBP-2040.  The offset/8 
//    is encoded in the UNWIND_X86_64_RBP_FRAME_OFFSET bits.  The registers saved
//    are encoded in the UNWIND_X86_64_RBP_FRAME_REGISTERS bits as five 3-bit entries.
//    Each entry contains which register to restore.  
// UNWIND_X86_64_MODE_STACK_IMMD:
//    A "frameless" (RBP not used as frame pointer) function with a small 
//    constant stack size.  To return, a constant (encoded in the compact 
//    unwind encoding) is added to the RSP. Then the return is done by 
//    popping the stack into the pc.
//    All non-volatile registers that need to be restored must have been saved
//    on the stack immediately after the return address.  The stack_size/8 is
//    encoded in the UNWIND_X86_64_FRAMELESS_STACK_SIZE (max stack size is 2048).
//    The number of registers saved is encoded in UNWIND_X86_64_FRAMELESS_STACK_REG_COUNT.
//    UNWIND_X86_64_FRAMELESS_STACK_REG_PERMUTATION constains which registers were
//    saved and their order.  
// UNWIND_X86_64_MODE_STACK_IND:
//    A "frameless" (RBP not used as frame pointer) function large constant 
//    stack size.  This case is like the previous, except the stack size is too
//    large to encode in the compact unwind encoding.  Instead it requires that 
//    the function contains "subq $nnnnnnnn,RSP" in its prolog.  The compact 
//    encoding contains the offset to the nnnnnnnn value in the function in
//    UNWIND_X86_64_FRAMELESS_STACK_SIZE.  
// UNWIND_X86_64_MODE_DWARF:
//    No compact unwind encoding is available.  Instead the low 24-bits of the
//    compact encoding is the offset of the DWARF FDE in the __eh_frame section.
//    This mode is never used in object files.  It is only generated by the 
//    linker in final linked images which have only DWARF unwind info for a
//    function.
//


// ARM64
//
// 1-bit: start
// 1-bit: has lsda
// 2-bit: personality index
//
// 4-bits: 4=frame-based, 3=DWARF, 2=frameless
//  frameless:
//        12-bits of stack size
//  frame-based:
//        4-bits D reg pairs saved
//        5-bits X reg pairs saved
//  DWARF:
//        24-bits offset of DWARF FDE in __eh_frame section
//
enum {
    UNWIND_ARM64_MODE_MASK                     = 0x0F000000,
    UNWIND_ARM64_MODE_FRAMELESS                = 0x02000000,
    UNWIND_ARM64_MODE_DWARF                    = 0x03000000,
    UNWIND_ARM64_MODE_FRAME                    = 0x04000000,

    UNWIND_ARM64_FRAME_X19_X20_PAIR            = 0x00000001,
    UNWIND_ARM64_FRAME_X21_X22_PAIR            = 0x00000002,
    UNWIND_ARM64_FRAME_X23_X24_PAIR            = 0x00000004,
    UNWIND_ARM64_FRAME_X25_X26_PAIR            = 0x00000008,
    UNWIND_ARM64_FRAME_X27_X28_PAIR            = 0x00000010,
    UNWIND_ARM64_FRAME_D8_D9_PAIR              = 0x00000100,
    UNWIND_ARM64_FRAME_D10_D11_PAIR            = 0x00000200,
    UNWIND_ARM64_FRAME_D12_D13_PAIR            = 0x00000400,
    UNWIND_ARM64_FRAME_D14_D15_PAIR            = 0x00000800,

    UNWIND_ARM64_FRAMELESS_STACK_SIZE_MASK     = 0x00FFF000,
    UNWIND_ARM64_DWARF_SECTION_OFFSET          = 0x00FFFFFF,
};
// For arm64 there are three modes for the compact unwind encoding:
// UNWIND_ARM64_MODE_FRAME:
//    This is a standard arm64 prolog where FP/LR are immediately pushed on the
//    stack, then SP is copied to FP. If there are any non-volatile registers
//    saved, then are copied into the stack frame in pairs in a contiguous
//    range right below the saved FP/LR pair.  Any subset of the five X pairs 
//    and four D pairs can be saved, but the memory layout must be in register
//    number order.  
// UNWIND_ARM64_MODE_FRAMELESS:
//    A "frameless" leaf function, where FP/LR are not saved. The return address 
//    remains in LR throughout the function. If any non-volatile registers
//    are saved, they must be pushed onto the stack before any stack space is
//    allocated for local variables.  The stack sized (including any saved
//    non-volatile registers) divided by 16 is encoded in the bits 
//    UNWIND_ARM64_FRAMELESS_STACK_SIZE_MASK.
// UNWIND_ARM64_MODE_DWARF:
//    No compact unwind encoding is available.  Instead the low 24-bits of the
//    compact encoding is the offset of the DWARF FDE in the __eh_frame section.
//    This mode is never used in object files.  It is only generated by the 
//    linker in final linked images which have only DWARF unwind info for a
//    function.
//





////////////////////////////////////////////////////////////////////////////////
//
//  Relocatable Object Files: __LD,__compact_unwind
//
////////////////////////////////////////////////////////////////////////////////

//
// A compiler can generated compact unwind information for a function by adding
// a "row" to the __LD,__compact_unwind section.  This section has the 
// S_ATTR_DEBUG bit set, so the section will be ignored by older linkers. 
// It is removed by the new linker, so never ends up in final executables. 
// This section is a table, initially with one row per function (that needs 
// unwind info).  The table columns and some conceptual entries are:
//
//     range-start               pointer to start of function/range
//     range-length              
//     compact-unwind-encoding   32-bit encoding  
//     personality-function      or zero if no personality function
//     lsda                      or zero if no LSDA data
//
// The length and encoding fields are 32-bits.  The other are all pointer sized. 
//
// In x86_64 assembly, these entry would look like:
//
//     .section __LD,__compact_unwind,regular,debug
//
//     #compact unwind for _foo
//     .quad    _foo
//     .set     L1,LfooEnd-_foo
//     .long    L1
//     .long    0x01010001
//     .quad    0
//     .quad    0
//
//     #compact unwind for _bar
//     .quad    _bar
//     .set     L2,LbarEnd-_bar
//     .long    L2
//     .long    0x01020011
//     .quad    __gxx_personality
//     .quad    except_tab1
//
//
// Notes: There is no need for any labels in the the __compact_unwind section.  
//        The use of the .set directive is to force the evaluation of the 
//        range-length at assembly time, instead of generating relocations.
//
// To support future compiler optimizations where which non-volatile registers 
// are saved changes within a function (e.g. delay saving non-volatiles until
// necessary), there can by multiple lines in the __compact_unwind table for one
// function, each with a different (non-overlapping) range and each with 
// different compact unwind encodings that correspond to the non-volatiles 
// saved at that range of the function.
//
// If a particular function is so wacky that there is no compact unwind way
// to encode it, then the compiler can emit traditional DWARF unwind info.  
// The runtime will use which ever is available.
//
// Runtime support for compact unwind encodings are only available on 10.6 
// and later.  So, the compiler should not generate it when targeting pre-10.6. 




////////////////////////////////////////////////////////////////////////////////
//
//  Final Linked Images: __TEXT,__unwind_info
//
////////////////////////////////////////////////////////////////////////////////

//
// The __TEXT,__unwind_info section is laid out for an efficient two level lookup.
// The header of the section contains a coarse index that maps function address
// to the page (4096 byte block) containing the unwind info for that function.  
//

#define UNWIND_SECTION_VERSION 1
struct unwind_info_section_header
{
    uint32_t    version;            // UNWIND_SECTION_VERSION
    uint32_t    commonEncodingsArraySectionOffset;
    uint32_t    commonEncodingsArrayCount;
    uint32_t    personalityArraySectionOffset;
    uint32_t    personalityArrayCount;
    uint32_t    indexSectionOffset;
    uint32_t    indexCount;
    // compact_unwind_encoding_t[]
    // uint32_t personalities[]
    // unwind_info_section_header_index_entry[]
    // unwind_info_section_header_lsda_index_entry[]
};

struct unwind_info_section_header_index_entry
{
    uint32_t        functionOffset;
    uint32_t        secondLevelPagesSectionOffset;  // section offset to start of regular or compress page
    uint32_t        lsdaIndexArraySectionOffset;    // section offset to start of lsda_index array for this range
};

struct unwind_info_section_header_lsda_index_entry
{
    uint32_t        functionOffset;
    uint32_t        lsdaOffset;
};

//
// There are two kinds of second level index pages: regular and compressed.
// A compressed page can hold up to 1021 entries, but it cannot be used
// if too many different encoding types are used.  The regular page holds
// 511 entries.
//

struct unwind_info_regular_second_level_entry
{
    uint32_t                    functionOffset;
    compact_unwind_encoding_t    encoding;
};

#define UNWIND_SECOND_LEVEL_REGULAR 2
struct unwind_info_regular_second_level_page_header
{
    uint32_t    kind;    // UNWIND_SECOND_LEVEL_REGULAR
    uint16_t    entryPageOffset;
    uint16_t    entryCount;
    // entry array
};

#define UNWIND_SECOND_LEVEL_COMPRESSED 3
struct unwind_info_compressed_second_level_page_header
{
    uint32_t    kind;    // UNWIND_SECOND_LEVEL_COMPRESSED
    uint16_t    entryPageOffset;
    uint16_t    entryCount;
    uint16_t    encodingsPageOffset;
    uint16_t    encodingsCount;
    // 32-bit entry array
    // encodings array
};

#define UNWIND_INFO_COMPRESSED_ENTRY_FUNC_OFFSET(entry)            (entry & 0x00FFFFFF)
#define UNWIND_INFO_COMPRESSED_ENTRY_ENCODING_INDEX(entry)        ((entry >> 24) & 0xFF)



#endif

