#ifndef __COMPAT_RFKILL_H
#define __COMPAT_RFKILL_H
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
#include_next <linux/rfkill.h>
#else
/* API only slightly changed since then */
#define rfkill_type old_rfkill_type
#define RFKILL_TYPE_ALL OLD_RFKILL_TYPE_ALL
#define RFKILL_TYPE_WLAN OLD_RFKILL_TYPE_WLAN
#define RFKILL_TYPE_BLUETOOTH OLD_RFKILL_TYPE_BLUETOOTH
#define RFKILL_TYPE_UWB OLD_RFKILL_TYPE_UWB
#define RFKILL_TYPE_WIMAX OLD_RFKILL_TYPE_WIMAX
#define RFKILL_TYPE_WWAN OLD_RFKILL_TYPE_WWAN
#define RFKILL_TYPE_GPS OLD_RFKILL_TYPE_GPS
#define RFKILL_TYPE_FM OLD_RFKILL_TYPE_FM
#define RFKILL_TYPE_NFC OLD_RFKILL_TYPE_NFC
#define NUM_RFKILL_TYPES OLD_NUM_RFKILL_TYPES
#include_next <linux/rfkill.h>
#undef rfkill_type
#undef RFKILL_TYPE_ALL
#undef RFKILL_TYPE_WLAN
#undef RFKILL_TYPE_BLUETOOTH
#undef RFKILL_TYPE_UWB
#undef RFKILL_TYPE_WIMAX
#undef RFKILL_TYPE_WWAN
#undef RFKILL_TYPE_GPS
#undef RFKILL_TYPE_FM
#undef RFKILL_TYPE_NFC
#undef NUM_RFKILL_TYPES
#define HAVE_OLD_RFKILL

/* this changes infrequently, backport manually */
enum rfkill_type {
	RFKILL_TYPE_ALL = 0,
	RFKILL_TYPE_WLAN,
	RFKILL_TYPE_BLUETOOTH,
	RFKILL_TYPE_UWB,
	RFKILL_TYPE_WIMAX,
	RFKILL_TYPE_WWAN,
	RFKILL_TYPE_GPS,
	RFKILL_TYPE_FM,
	RFKILL_TYPE_NFC,
	NUM_RFKILL_TYPES,
};

static inline struct rfkill * __must_check
backport_rfkill_alloc(const char *name,
		      struct device *parent,
		      const enum rfkill_type type,
		      const struct rfkill_ops *ops,
		      void *ops_data)
{
#ifdef HAVE_OLD_RFKILL
	if ((unsigned int)type >= (unsigned int)OLD_NUM_RFKILL_TYPES)
		return ERR_PTR(-ENODEV);
	return rfkill_alloc(name, parent, (enum old_rfkill_type)type,
			    ops, ops_data);
#else
	return ERR_PTR(-ENODEV);
#endif
}
#define rfkill_alloc backport_rfkill_alloc

static inline int __must_check backport_rfkill_register(struct rfkill *rfkill)
{
	if (rfkill == ERR_PTR(-ENODEV))
		return 0;
#ifdef HAVE_OLD_RFKILL
	return rfkill_register(rfkill);
#else
	return -EINVAL;
#endif
}
#define rfkill_register backport_rfkill_register

static inline void backport_rfkill_pause_polling(struct rfkill *rfkill)
{
#ifdef HAVE_OLD_RFKILL
	rfkill_pause_polling(rfkill);
#endif
}
#define rfkill_pause_polling backport_rfkill_pause_polling

static inline void backport_rfkill_resume_polling(struct rfkill *rfkill)
{
#ifdef HAVE_OLD_RFKILL
	rfkill_resume_polling(rfkill);
#endif
}
#define rfkill_resume_polling backport_rfkill_resume_polling

static inline void backport_rfkill_unregister(struct rfkill *rfkill)
{
#ifdef HAVE_OLD_RFKILL
	if (rfkill == ERR_PTR(-ENODEV))
		return;
	rfkill_unregister(rfkill);
#endif
}
#define rfkill_unregister backport_rfkill_unregister

static inline void backport_rfkill_destroy(struct rfkill *rfkill)
{
#ifdef HAVE_OLD_RFKILL
	if (rfkill == ERR_PTR(-ENODEV))
		return;
	rfkill_destroy(rfkill);
#endif
}
#define rfkill_destroy backport_rfkill_destroy

static inline bool backport_rfkill_set_hw_state(struct rfkill *rfkill,
						bool blocked)
{
#ifdef HAVE_OLD_RFKILL
	if (rfkill != ERR_PTR(-ENODEV))
		return rfkill_set_hw_state(rfkill, blocked);
#endif
	return blocked;
}
#define rfkill_set_hw_state backport_rfkill_set_hw_state

static inline bool backport_rfkill_set_sw_state(struct rfkill *rfkill,
						bool blocked)
{
#ifdef HAVE_OLD_RFKILL
	if (rfkill != ERR_PTR(-ENODEV))
		return rfkill_set_sw_state(rfkill, blocked);
#endif
	return blocked;
}
#define rfkill_set_sw_state backport_rfkill_set_sw_state

static inline void backport_rfkill_init_sw_state(struct rfkill *rfkill,
						 bool blocked)
{
#ifdef HAVE_OLD_RFKILL
	if (rfkill != ERR_PTR(-ENODEV))
		rfkill_init_sw_state(rfkill, blocked);
#endif
}
#define rfkill_init_sw_state backport_rfkill_init_sw_state

static inline void backport_rfkill_set_states(struct rfkill *rfkill,
					      bool sw, bool hw)
{
#ifdef HAVE_OLD_RFKILL
	if (rfkill != ERR_PTR(-ENODEV))
		rfkill_set_states(rfkill, sw, hw);
#endif
}
#define rfkill_set_states backport_rfkill_set_states

static inline bool backport_rfkill_blocked(struct rfkill *rfkill)
{
#ifdef HAVE_OLD_RFKILL
	if (rfkill != ERR_PTR(-ENODEV))
		return rfkill_blocked(rfkill);
#endif
	return false;
}
#define rfkill_blocked backport_rfkill_blocked
#endif

#endif
