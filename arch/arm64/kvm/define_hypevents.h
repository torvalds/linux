/* SPDX-License-Identifier: GPL-2.0 */

#define REMOTE_EVENT_INCLUDE_FILE arch/arm64/include/asm/kvm_hypevents.h

#define REMOTE_EVENT_SECTION "_hyp_events"

#define HE_STRUCT(__args)		__args
#define HE_PRINTK(__args...)		__args
#define he_field			re_field

#define HYP_EVENT(__name, __proto, __struct, __assign, __printk) \
	REMOTE_EVENT(__name, 0, RE_STRUCT(__struct), RE_PRINTK(__printk))

#define HYP_EVENT_MULTI_READ
#include <trace/define_remote_events.h>
#undef HYP_EVENT_MULTI_READ
