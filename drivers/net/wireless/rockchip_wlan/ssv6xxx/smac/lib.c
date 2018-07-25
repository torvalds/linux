/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <ssv6200.h>
#include "lib.h"
struct sk_buff *ssv_skb_alloc(s32 len)
{
    struct sk_buff *skb;
    skb = __dev_alloc_skb(len + 128 , GFP_KERNEL);
    if (skb != NULL) {
        skb_put(skb,0x20);
        skb_pull(skb,0x20);
    }
    return skb;
}
void ssv_skb_free(struct sk_buff *skb)
{
    dev_kfree_skb_any(skb);
}
