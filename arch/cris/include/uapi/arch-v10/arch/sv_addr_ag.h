/*!**************************************************************************
*!                                                            
*! MACROS:
*!   IO_MASK(reg,field)
*!   IO_STATE(reg,field,state)
*!   IO_EXTRACT(reg,field,val)
*!   IO_STATE_VALUE(reg,field,state)
*!   IO_BITNR(reg,field)
*!   IO_WIDTH(reg,field)
*!   IO_FIELD(reg,field,val)
*!   IO_RD(reg)
*!   All moderegister addresses and fields of these.
*!
*!**************************************************************************/

#ifndef __sv_addr_ag_h__
#define __sv_addr_ag_h__


#define __test_sv_addr__ 0

/*------------------------------------------------------------
!* General macros to manipulate moderegisters.
!*-----------------------------------------------------------*/

/* IO_MASK returns a mask for a specified bitfield in a register.
   Note that this macro doesn't work when field width is 32 bits. */
#define IO_MASK(reg, field) IO_MASK_ (reg##_, field##_)
#define IO_MASK_(reg_, field_) \
    ( ( ( 1 << reg_##_##field_##_WIDTH ) - 1 ) << reg_##_##field_##_BITNR )

/* IO_STATE returns a constant corresponding to a one of the symbolic
   states that the bitfield can have. (Shifted to correct position)  */
#define IO_STATE(reg, field, state) IO_STATE_ (reg##_, field##_, _##state)
#define IO_STATE_(reg_, field_, _state) \
    ( reg_##_##field_##_state << reg_##_##field_##_BITNR )

/* IO_EXTRACT returns the masked and shifted value corresponding to the
   bitfield can have. */
#define IO_EXTRACT(reg, field, val) IO_EXTRACT_ (reg##_, field##_, val)
#define IO_EXTRACT_(reg_, field_, val) ( (( ( ( 1 << reg_##_##field_##_WIDTH ) \
     - 1 ) << reg_##_##field_##_BITNR ) & (val)) >> reg_##_##field_##_BITNR )

/* IO_STATE_VALUE returns a constant corresponding to a one of the symbolic
   states that the bitfield can have. (Not shifted)  */
#define IO_STATE_VALUE(reg, field, state) \
    IO_STATE_VALUE_ (reg##_, field##_, _##state)
#define IO_STATE_VALUE_(reg_, field_, _state) ( reg_##_##field_##_state )

/* IO_FIELD shifts the val parameter to be aligned with the bitfield
   specified. */
#define IO_FIELD(reg, field, val) IO_FIELD_ (reg##_, field##_, val)
#define IO_FIELD_(reg_, field_, val) ((val) << reg_##_##field_##_BITNR)

/* IO_BITNR returns the starting bitnumber of a bitfield. Bit 0 is
   LSB and the returned bitnumber is LSB of the field. */
#define IO_BITNR(reg, field) IO_BITNR_ (reg##_, field##_)
#define IO_BITNR_(reg_, field_) (reg_##_##field_##_BITNR)

/* IO_WIDTH returns the width, in bits, of a bitfield. */
#define IO_WIDTH(reg, field) IO_WIDTH_ (reg##_, field##_)
#define IO_WIDTH_(reg_, field_) (reg_##_##field_##_WIDTH)

/*--- Obsolete. Kept for backward compatibility. ---*/
/* Reads (or writes) a byte/uword/udword from the specified mode
   register. */
#define IO_RD(reg) (*(volatile u32*)(reg))
#define IO_RD_B(reg) (*(volatile u8*)(reg))
#define IO_RD_W(reg) (*(volatile u16*)(reg))
#define IO_RD_D(reg) (*(volatile u32*)(reg))

/*------------------------------------------------------------
!* Start addresses of the different memory areas.
!*-----------------------------------------------------------*/

#define MEM_CSE0_START (0x00000000)
#define MEM_CSE0_SIZE (0x04000000)
#define MEM_CSE1_START (0x04000000)
#define MEM_CSE1_SIZE (0x04000000)
#define MEM_CSR0_START (0x08000000)
#define MEM_CSR1_START (0x0c000000)
#define MEM_CSP0_START (0x10000000)
#define MEM_CSP1_START (0x14000000)
#define MEM_CSP2_START (0x18000000)
#define MEM_CSP3_START (0x1c000000)
#define MEM_CSP4_START (0x20000000)
#define MEM_CSP5_START (0x24000000)
#define MEM_CSP6_START (0x28000000)
#define MEM_CSP7_START (0x2c000000)
#define MEM_DRAM_START (0x40000000)

#define MEM_NON_CACHEABLE (0x80000000)

/*------------------------------------------------------------
!* Type casts used in mode register macros, making pointer
!* dereferencing possible. Empty in assembler.
!*-----------------------------------------------------------*/

#ifndef __ASSEMBLER__
# define  IO_TYPECAST_UDWORD  (volatile u32*)
# define  IO_TYPECAST_RO_UDWORD  (const volatile u32*)
# define  IO_TYPECAST_UWORD  (volatile u16*)
# define  IO_TYPECAST_RO_UWORD  (const volatile u16*)
# define  IO_TYPECAST_BYTE  (volatile u8*)
# define  IO_TYPECAST_RO_BYTE  (const volatile u8*)
#else
# define  IO_TYPECAST_UDWORD
# define  IO_TYPECAST_RO_UDWORD
# define  IO_TYPECAST_UWORD
# define  IO_TYPECAST_RO_UWORD
# define  IO_TYPECAST_BYTE
# define  IO_TYPECAST_RO_BYTE
#endif

/*------------------------------------------------------------*/

#include <arch/sv_addr.agh>

#if __test_sv_addr__
/* IO_MASK( R_BUS_CONFIG , CE ) */
IO_MASK( R_WAITSTATES , SRAM_WS )
IO_MASK( R_TEST , W32 )

IO_STATE( R_BUS_CONFIG, CE, DISABLE )
IO_STATE( R_BUS_CONFIG, CE, ENABLE )

IO_STATE( R_DRAM_TIMING, REF, IVAL2 )

IO_MASK( R_DRAM_TIMING, REF )

IO_MASK( R_EXT_DMA_0_STAT, TFR_COUNT ) >> IO_BITNR( R_EXT_DMA_0_STAT, TFR_COUNT )

IO_RD(R_EXT_DMA_0_STAT) & IO_MASK( R_EXT_DMA_0_STAT, S ) 
   == IO_STATE( R_EXT_DMA_0_STAT, S, STARTED )
#endif


#endif  /* ifndef __sv_addr_ag_h__ */

