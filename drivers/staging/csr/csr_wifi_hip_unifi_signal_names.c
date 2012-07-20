/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include "csr_wifi_hip_unifi.h"

struct sig_name
{
    s16             id;
    const CsrCharString *name;
};

static const struct sig_name Unifi_bulkcmd_names[] = {
    {  0, "SignalCmd" },
    {  1, "CopyToHost" },
    {  2, "CopyToHostAck" },
    {  3, "CopyFromHost" },
    {  4, "CopyFromHostAck" },
    {  5, "ClearSlot" },
    {  6, "CopyOverlay" },
    {  7, "CopyOverlayAck" },
    {  8, "CopyFromHostAndClearSlot" },
    {  15, "Padding" }
};

const CsrCharString* lookup_bulkcmd_name(u16 id)
{
    if (id < 9)
    {
        return Unifi_bulkcmd_names[id].name;
    }
    if (id == 15)
    {
        return "Padding";
    }

    return "UNKNOWN";
}


