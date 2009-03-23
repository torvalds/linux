/****************************************************************************

    global project definition file

    12.06.1998   -rs
    11.02.2002   r.d. Erweiterungen, Ergaenzungen
    20.08.2002   SYS TEC electronic -as
                 Definition Schluesselwort 'GENERIC'
                 fuer das Erzeugen von Generic Pointer
    28.08.2002   r.d. erweiterter SYS TEC Debug Code
    16.09.2002   r.d. komplette Uebersetzung in Englisch
    11.04.2003   f.j. Ergaenzung fuer Mitsubishi NC30 Compiler
    17.06.2003   -rs  Definition von Basistypen in <#ifndef _WINDEF_> gesetzt
    16.04.2004   r.d. Ergaenzung fuer Borland C++ Builder
    30.08.2004   -rs  TRACE5 eingefügt
    23.12.2005   d.k. Definitions for IAR compiler

    $Id: global.h,v 1.6 2008/11/07 13:55:56 D.Krueger Exp $

****************************************************************************/

#ifndef _GLOBAL_H_
#define _GLOBAL_H_

//---------------------------------------------------------------------------
//  elements of defines for development system
//---------------------------------------------------------------------------

// these defines are necessary to check some of characteristics of the development system
#define _DEV_BIGEND_            0x80000000L	// big endian (motorolla format)
#define _DEV_ALIGNMENT_4_       0x00400000L	//                  the CPU needs alignment of 4 bytes
#define _DEV_ONLY_INT_MAIN_     0x00004000L	//                  the compiler needs "int main(int)" instead of "void main(void)"
#define _DEV_COMMA_EXT_         0x00002000L	//                  support of last comma in struct predefinition
#define _DEV_64BIT_SUPPORT_     0x00001000L	//                  support of 64 bit operations
#define _DEV_BIT64_             0x00000400L	// count of bits:   64 bit
#define _DEV_BIT32_             0x00000300L	//                  32 bit
#define _DEV_BIT16_             0x00000200L	//                  16 bit
#define _DEV_BIT8_              0x00000100L	//                  8 bit
#define _DEV_RVCT_ARM_          0x0000001CL	//                  RealView ARM
#define _DEV_RENESASM32C        0x0000001BL	// compiler from:   Renesas
#define _DEV_GNUC_MIPS2_        0x0000001AL	//                  GNU for MIPS2
#define _DEV_MPLAB_C30_         0x00000019L	//                  MPLAB C30 for Microchip dsPIC33F series
#define _DEV_GNUC_TC_           0x00000018L	//                  GNU for Infineon TriCore
#define _DEV_GNUC_X86_          0x00000017L	//                  GNU for I386
#define _DEV_IAR_ARM_           0x00000016L	//                  ARM IAR C/C++ Compiler
#define _DEV_PARADGM_X86        0x00000015L	//                  Paradigm C/C++ for Beck 1x3
#define _DEV_GNUC_CF_           0x00000014L	//                  GNU for Coldfire
#define _DEV_KEIL_ARM_          0x00000013L	//                  Keil ARM
#define _DEV_MSEVC_             0x00000012L	//                  Microsoft embedded Visual C/C++
#define _DEV_HIGHTEC_GNUC_X86_  0x00000011L	//                  Hightec elf386 gcc
#define _DEV_MSVC_RTX_          0x00000010L	//                  VC600 + RTX
#define _DEV_MSVC_V1_5_         0x0000000FL	//                  Microsoft Visual C/C++ V1.5
#define _DEV_GNUC_ARM7_         0x0000000EL	//                  GNU Compiler gcc for ARM7
#define _DEV_METROWERKS_CW_     0x0000000DL	//                  Metrowerks Code Warrior
#define _DEV_MITSUBISHIM16C_    0x0000000CL	//compiler from:    Mitsubishi
#define _DEV_GNUC_C16X_         0x0000000BL	//                  GNU Compiler gcc166 for Infineon C16x
#define _DEV_LINUX_GCC_         0x0000000AL	//                  Linux GNU Compiler gcc
#define _DEV_GNUC_MPC5X5        0x00000009L	//                  GNU for Motorola PPC5x5
#define _DEV_TASKINGM16C_       0x00000008L	//                  Tasking for Mitsubishi M16C
#define _DEV_FUJITSU_           0x00000007L	//                  Fujitsu
#define _DEV_TASKING8_          0x00000006L	//                  Tasking 8051
#define _DEV_TASKING16_         0x00000005L	//                  Tasking 166
#define _DEV_KEIL8_             0x00000004L	//                  Keil C51
#define _DEV_KEIL16_            0x00000003L	//                  Keil C166
#define _DEV_BORLANDC_          0x00000002L	//                  Borland C/C++
#define _DEV_MSVC16_            0x00000001L	//                  Microsoft Visual C/C++
#define _DEV_MSVC32_            0x00000000L	//                  Microsoft Visual C/C++

// these defines can be used to mask previous elements
#define _DEV_MASK_COMPILER      0x000000FFL
#define _DEV_MASK_BITCOUNT      0x00000F00L
#define _DEV_MASK_ADDSUPPORT    0x0000F000L
#define _DEV_MASK_ALIGNMENT     0x00F00000L

//---------------------------------------------------------------------------
//  defines for development system (DEV_SYSTEM) including previous elements
//---------------------------------------------------------------------------

#define _DEV_WIN16_             (_DEV_BIT16_ | _DEV_MSVC16_                  )
#define _DEV_WIN32_             (_DEV_BIT32_ | _DEV_MSVC32_                   | _DEV_64BIT_SUPPORT_ | _DEV_COMMA_EXT_)
#define _DEV_MSVC_DOS_          (_DEV_BIT32_ | _DEV_MSVC_V1_5_               )
#define _DEV_BORLAND_DOS_       (_DEV_BIT32_ | _DEV_BORLANDC_                )	//| _DEV_64BIT_SUPPORT_ | _DEV_COMMA_EXT_)
#define _DEV_KEIL_C51X_         (_DEV_BIT8_  | _DEV_KEIL8_     | _DEV_BIGEND_ | _DEV_COMMA_EXT_)	// at least C51 version 7.05 supports comma extension
#define _DEV_KEIL_C16X_         (_DEV_BIT16_ | _DEV_KEIL16_                   | _DEV_COMMA_EXT_)	// at least C166 version 5.03 supports comma extension
#define _DEV_TASKING_C51X_      (_DEV_BIT8_  | _DEV_TASKING8_  | _DEV_BIGEND_)
#define _DEV_TASKING_C16X_      (_DEV_BIT16_ | _DEV_TASKING16_               )
#define _DEV_FUJITSU_F590_      (_DEV_BIT8_  | _DEV_FUJITSU_                  | _DEV_COMMA_EXT_)	// softune is not able to support 64 bit variables QWORD !!!
//f.j.29.04.03 M16C kann effektiv mit Bytes umgehen
//#define _DEV_TASKING_M16C_      (_DEV_BIT16_ | _DEV_TASKINGM16C_             )
#define _DEV_TASKING_M16C_      (_DEV_BIT8_  | _DEV_TASKINGM16C_             )
#define _DEV_MITSUBISHI_M16C_   (_DEV_BIT8_  | _DEV_MITSUBISHIM16C_          )
#define _DEV_GNU_MPC5X5_        (_DEV_BIT32_ | _DEV_GNUC_MPC5X5| _DEV_BIGEND_ | _DEV_64BIT_SUPPORT_ | _DEV_COMMA_EXT_)
#define _DEV_LINUX_             (_DEV_BIT32_ | _DEV_LINUX_GCC_                | _DEV_64BIT_SUPPORT_ | _DEV_COMMA_EXT_)
#define _DEV_GNU_C16X_          (_DEV_BIT16_ | _DEV_GNUC_C16X_               )	//| _DEV_COMMA_EXT_)
#define _DEV_MCW_MPC5X5_        (_DEV_BIT32_ | _DEV_METROWERKS_CW_           )	//| _DEV_64BIT_SUPPORT_ | _DEV_COMMA_EXT_)
#define _DEV_GNU_ARM7_          (_DEV_BIT32_ | _DEV_GNUC_ARM7_                | _DEV_64BIT_SUPPORT_ | _DEV_COMMA_EXT_ | _DEV_ONLY_INT_MAIN_)
#define _DEV_WIN32_RTX_         (_DEV_BIT32_ | _DEV_MSVC_RTX_                )	//| _DEV_64BIT_SUPPORT_ | _DEV_COMMA_EXT_)
#define _DEV_HIGHTEC_X86_       (_DEV_BIT32_ | _DEV_HIGHTEC_GNUC_X86_        )	//| _DEV_64BIT_SUPPORT_ | _DEV_COMMA_EXT_)
#define _DEV_WIN_CE_            (_DEV_BIT32_ | _DEV_MSEVC_                   )	//| _DEV_64BIT_SUPPORT_ | _DEV_COMMA_EXT_)
#define _DEV_KEIL_CARM_         (_DEV_BIT32_ | _DEV_KEIL_ARM_                 | _DEV_64BIT_SUPPORT_ | _DEV_COMMA_EXT_)
#define _DEV_IAR_CARM_          (_DEV_BIT32_ | _DEV_IAR_ARM_                  | _DEV_64BIT_SUPPORT_ | _DEV_COMMA_EXT_)
#define _DEV_RVCT_CARM_         (_DEV_BIT32_ | _DEV_RVCT_ARM_                 | _DEV_64BIT_SUPPORT_ | _DEV_COMMA_EXT_ | _DEV_ONLY_INT_MAIN_)
#define _DEV_MCW_MCF5XXX_       (_DEV_BIT32_ | _DEV_METROWERKS_CW_           )	//| _DEV_64BIT_SUPPORT_ | _DEV_COMMA_EXT_)
#define _DEV_GNU_CF5282_        (_DEV_BIT32_ | _DEV_GNUC_CF_   | _DEV_BIGEND_)
#define _DEV_PAR_BECK1X3_       (_DEV_BIT16_ | _DEV_PARADGM_X86)
#define _DEV_GNU_CF548X_        (_DEV_BIT32_ | _DEV_GNUC_CF_   | _DEV_BIGEND_ | _DEV_64BIT_SUPPORT_ | _DEV_COMMA_EXT_)
#define _DEV_GNU_I386_          (_DEV_BIT32_ | _DEV_GNUC_X86_                 | _DEV_64BIT_SUPPORT_ | _DEV_COMMA_EXT_ | _DEV_ONLY_INT_MAIN_)
#define _DEV_GNU_TRICORE_       (_DEV_BIT32_ | _DEV_GNUC_TC_                  | _DEV_64BIT_SUPPORT_ | _DEV_COMMA_EXT_ | _DEV_ONLY_INT_MAIN_ | _DEV_ALIGNMENT_4_)
#define _DEV_MPLAB_DSPIC33F_    (_DEV_BIT16_ | _DEV_MPLAB_C30_               )	//| _DEV_COMMA_EXT_)
#define _DEV_GNU_MIPSEL_        (_DEV_BIT32_ | _DEV_GNUC_MIPS2_     | _DEV_BIGEND_ | _DEV_64BIT_SUPPORT_ | _DEV_COMMA_EXT_ | _DEV_ONLY_INT_MAIN_)

#define _DEV_RENESAS_M32C_      (_DEV_BIT32_ | _DEV_RENESASM32C)

//---------------------------------------------------------------------------
//  usefull macros
//---------------------------------------------------------------------------

#define CHECK_IF_ONLY_INT_MAIN()    (DEV_SYSTEM & _DEV_ONLY_INT_MAIN_)
#define CHECK_MEMORY_ALINMENT()     (DEV_SYSTEM & _DEV_MASK_ALIGNMENT)

//---------------------------------------------------------------------------
//  defines for target system (TARGET_SYSTEM)
//---------------------------------------------------------------------------
#define _LINUX_              1

//---------------------------------------------------------------------------
//  definitions for function inlining
//---------------------------------------------------------------------------

#define LINUX_SYSTEM		// define 'LINUX_SYSTEM' uniform for all Linux based systems
	// r.d.: We will need an other solution here! There are two sections here which do check the preproc-definitions:
	//     LINUX and __linux__ . The first one was Linux for PC, the second one is this section for embedded Linux (MCF5xxx).
	//     But Linux for PC does not need the definitions for embedded Linux.

#define TARGET_SYSTEM       _LINUX_	// Linux definition
#define DEV_SYSTEM          _DEV_LINUX_

#ifndef QWORD
#define QWORD long long int
#endif

    // ------------------ GNUC for I386 ---------------------------------------------

#define ROM			// code or variables mapped to ROM (i.e. flash)
					// usage: CONST BYTE ROM foo = 0x00;
#define HWACC			// hardware access through external memory (i.e. CAN)

	// These types can be adjusted by users to match application requirements. The goal is to
	// minimize code memory and maximize speed.
#define GENERIC			// generic pointer to point to application data
					// Variables with this attribute can be located in external
					// or internal data memory.
#define MEM			// Memory attribute to optimize speed and code of pointer access.

#ifndef NEAR
#define NEAR			// variables mapped to internal data storage location
#endif

#ifndef FAR
#define FAR			// variables mapped to external data storage location
#endif

#ifndef CONST
#define CONST const		// variables mapped to ROM (i.e. flash)
#endif

#define REENTRANT

#define TRACE  printk

#ifndef SHORT
#define SHORT short int
#endif
#ifndef USHORT
#define USHORT unsigned short int
#endif
#ifndef INT
#define INT int
#endif
#ifndef UINT
#define UINT unsigned int
#endif
#ifndef LONG
#define LONG long int
#endif
#ifndef ULONG
#define ULONG unsigned long int
#endif
    // --- logic types ---
#ifndef BYTE
#define BYTE unsigned char
#endif
#ifndef WORD
#define WORD unsigned short int
#endif
#ifndef DWORD
#define DWORD unsigned long int
#endif
#ifndef BOOL
#define BOOL unsigned char
#endif
    // --- alias types ---
#ifndef TRUE
#define TRUE  0xFF
#endif
#ifndef FALSE
#define FALSE 0x00
#endif
#ifndef _TIME_OF_DAY_DEFINED_
typedef struct {
	unsigned long int m_dwMs;
	unsigned short int m_wDays;

} tTimeOfDay;

#define _TIME_OF_DAY_DEFINED_

#endif

//---------------------------------------------------------------------------
//  Definition von TRACE
//---------------------------------------------------------------------------

#ifndef NDEBUG

#ifndef TRACE0
#define TRACE0(p0)                      TRACE(p0)
#endif

#ifndef TRACE1
#define TRACE1(p0, p1)                  TRACE(p0, p1)
#endif

#ifndef TRACE2
#define TRACE2(p0, p1, p2)              TRACE(p0, p1, p2)
#endif

#ifndef TRACE3
#define TRACE3(p0, p1, p2, p3)          TRACE(p0, p1, p2, p3)
#endif

#ifndef TRACE4
#define TRACE4(p0, p1, p2, p3, p4)      TRACE(p0, p1, p2, p3, p4)
#endif

#ifndef TRACE5
#define TRACE5(p0, p1, p2, p3, p4, p5)  TRACE(p0, p1, p2, p3, p4, p5)
#endif

#ifndef TRACE6
#define TRACE6(p0, p1, p2, p3, p4, p5, p6)  TRACE(p0, p1, p2, p3, p4, p5, p6)
#endif

#else

#ifndef TRACE0
#define TRACE0(p0)
#endif

#ifndef TRACE1
#define TRACE1(p0, p1)
#endif

#ifndef TRACE2
#define TRACE2(p0, p1, p2)
#endif

#ifndef TRACE3
#define TRACE3(p0, p1, p2, p3)
#endif

#ifndef TRACE4
#define TRACE4(p0, p1, p2, p3, p4)
#endif

#ifndef TRACE5
#define TRACE5(p0, p1, p2, p3, p4, p5)
#endif

#ifndef TRACE6
#define TRACE6(p0, p1, p2, p3, p4, p5, p6)
#endif

#endif

//---------------------------------------------------------------------------
//  definition of ASSERT
//---------------------------------------------------------------------------

#ifndef ASSERT
#define ASSERT(p)
#endif

//---------------------------------------------------------------------------
//  SYS TEC extensions
//---------------------------------------------------------------------------

// This macro doesn't print out C-file and line number of the failed assertion
// but a string, which exactly names the mistake.
#ifndef NDEBUG

#define ASSERTMSG(expr,string)  if (!(expr)) {\
                                        PRINTF0 ("Assertion failed: " string );\
                                        while (1);}
#else
#define ASSERTMSG(expr,string)
#endif

//---------------------------------------------------------------------------

#endif // #ifndef _GLOBAL_H_

// Please keep an empty line at the end of this file.
