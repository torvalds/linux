//------------------------------------------------------------------------------
// <copyright file="hif_bmi_diag_access.c" company="Atheros">
//    Copyright (c) 2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// common Diagnostic access handling for register-based HIFs
// This module implements diagnostic accesses on behalf of the diagnostic window module for
// HIFs that are based on a register access model
//
//
// Author(s): ="Atheros"
//==============================================================================


#include "a_config.h"
#include "athdefs.h"
#include "a_types.h"
#include "a_osapi.h"
#define ATH_MODULE_NAME misc
#include "a_debug.h"

#include "targaddrs.h"
#include "hif.h"
#include "host_reg_table.h"

#define CPU_DBG_SEL_ADDRESS                      0x00000483
#define CPU_DBG_ADDRESS                          0x00000484

#ifdef USE_4BYTE_REGISTER_ACCESS
/* set the window address register (using 4-byte register access ).
 * This mitigates host interconnect issues with non-4byte aligned bus requests, some
 * interconnects use bus adapters that impose strict limitations.
 * Since diag window access is not intended for performance critical operations, the 4byte mode should
 * be satisfactory even though it generates 4X the bus activity.  */
static A_STATUS ar6000_SetAddressWindowRegister(HIF_DEVICE *hifDevice, A_UINT32 RegisterAddr, A_UINT32 Address)
{
    A_STATUS status;
    static A_UINT8 addrValue[4];
    A_INT32 i;
    static A_UINT32 address;

    address = Address;


        /* write bytes 1,2,3 of the register to set the upper address bytes, the LSB is written
         * last to initiate the access cycle */

    for (i = 1; i <= 3; i++) {
            /* fill the buffer with the address byte value we want to hit 4 times*/
        addrValue[0] = ((A_UINT8 *)&Address)[i];
        addrValue[1] = addrValue[0];
        addrValue[2] = addrValue[0];
        addrValue[3] = addrValue[0];

            /* hit each byte of the register address with a 4-byte write operation to the same address,
             * this is a harmless operation */
        status = HIFReadWrite(hifDevice,
                              RegisterAddr+i,
                              addrValue,
                              4,
                              HIF_WR_SYNC_BYTE_FIX,
                              NULL);
        if (status != A_OK) {
            break;
        }
    }

    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Cannot write initial bytes of 0x%x to window reg: 0x%X \n",
            Address, RegisterAddr));
        return status;
    }

        /* write the address register again, this time write the whole 4-byte value.
         * The effect here is that the LSB write causes the cycle to start, the extra
         * 3 byte write to bytes 1,2,3 has no effect since we are writing the same values again */
    status = HIFReadWrite(hifDevice,
                          RegisterAddr,
                          (A_UCHAR *)(&address),
                          4,
                          HIF_WR_SYNC_BYTE_INC,
                          NULL);

    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Cannot write 0x%x to window reg: 0x%X \n",
            Address, RegisterAddr));
        return status;
    }

    return A_OK;
}
#else

/* set the window address register */
A_STATUS ar6000_SetAddressWindowRegister(HIF_DEVICE *hifDevice, A_UINT32 RegisterAddr, A_UINT32 Address)
{
    A_STATUS status;

        /* write bytes 1,2,3 of the register to set the upper address bytes, the LSB is written
         * last to initiate the access cycle */
    status = HIFReadWrite(hifDevice,
                          RegisterAddr+1,  /* write upper 3 bytes */
                          ((A_UCHAR *)(&Address))+1,
                          sizeof(A_UINT32)-1,
                          HIF_WR_SYNC_BYTE_INC,
                          NULL);

    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Cannot write initial bytes of 0x%x to window reg: 0x%X \n",
             RegisterAddr, Address));
        return status;
    }

        /* write the LSB of the register, this initiates the operation */
    status = HIFReadWrite(hifDevice,
                          RegisterAddr,
                          (A_UCHAR *)(&Address),
                          sizeof(A_UINT8),
                          HIF_WR_SYNC_BYTE_INC,
                          NULL);

    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Cannot write 0x%x to window reg: 0x%X \n",
            RegisterAddr, Address));
        return status;
    }

    return A_OK;
}
#endif //USE_4BYTE_REGISTER_ACCESS

/*
 * Read from the AR6000 through its diagnostic window.
 * No cooperation from the Target is required for this.
 */
A_STATUS
HIFDiagReadAccess(HIF_DEVICE *hifDevice, A_UINT32 address, A_UINT32 *data)
{
    A_STATUS status;
    static A_UINT32 readvalue;

        /* set window register to start read cycle */
    status = ar6000_SetAddressWindowRegister(hifDevice,
                                             WINDOW_READ_ADDR_ADDRESS,
                                             address);

    if (status != A_OK) {
        return status;
    }

        /* read the data */
    status = HIFReadWrite(hifDevice,
                          WINDOW_DATA_ADDRESS,
                          (A_UCHAR *)&readvalue,
                          sizeof(A_UINT32),
                          HIF_RD_SYNC_BYTE_INC,
                          NULL);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Cannot read from WINDOW_DATA_ADDRESS\n"));
        return status;
    }

    *data = readvalue;

    return status;
}


/*
 * Write to the AR6000 through its diagnostic window.
 * No cooperation from the Target is required for this.
 */
A_STATUS  HIFDiagWriteAccess(HIF_DEVICE *hifDevice, A_UINT32 address, A_UINT32 data)
{
    A_STATUS status;
    static A_UINT32 writeValue;

    writeValue = data;

        /* set write data */
    status = HIFReadWrite(hifDevice,
                          WINDOW_DATA_ADDRESS,
                          (A_UCHAR *)&writeValue,
                          sizeof(A_UINT32),
                          HIF_WR_SYNC_BYTE_INC,
                          NULL);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Cannot write 0x%x to WINDOW_DATA_ADDRESS\n", data));
        return status;
    }

        /* set window register, which starts the write cycle */
    return ar6000_SetAddressWindowRegister(hifDevice,
                                           WINDOW_WRITE_ADDR_ADDRESS,
                                           address);
}


/* TODO .. the following APIs are only available on register-based HIFs where the CPU_DBG_SEL_ADDRESS
 * register is available */

A_STATUS
ar6k_ReadTargetRegister(HIF_DEVICE *hifDevice, int regsel, A_UINT32 *regval)
{
    A_STATUS status;
    A_UCHAR vals[4];
    A_UCHAR register_selection[4];

    register_selection[0] = register_selection[1] = register_selection[2] = register_selection[3] = (regsel & 0xff);
    status = HIFReadWrite(hifDevice,
                          CPU_DBG_SEL_ADDRESS,
                          register_selection,
                          4,
                          HIF_WR_SYNC_BYTE_FIX,
                          NULL);

    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Cannot write CPU_DBG_SEL (%d)\n", regsel));
        return status;
    }

    status = HIFReadWrite(hifDevice,
                          CPU_DBG_ADDRESS,
                          (A_UCHAR *)vals,
                          sizeof(vals),
                          HIF_RD_SYNC_BYTE_INC,
                          NULL);
    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_LOG_ERR, ("Cannot read from CPU_DBG_ADDRESS\n"));
        return status;
    }

    *regval = vals[0]<<0 | vals[1]<<8 | vals[2]<<16 | vals[3]<<24;

    return status;
}

void
ar6k_FetchTargetRegs(HIF_DEVICE *hifDevice, A_UINT32 *targregs)
{
    int i;
    A_UINT32 val;

    for (i=0; i<AR6003_FETCH_TARG_REGS_COUNT; i++) {
        val=0xffffffff;
        (void)ar6k_ReadTargetRegister(hifDevice, i, &val);
        targregs[i] = val;
    }
}


