#ifndef __USB_SWITCH_H__
#define __USB_SWITCH_H__

extern struct class *sec_class;

enum usb_path_t {
	USB_PATH_NONE = 0,
	USB_PATH_ADCCHECK = (1 << 28),
	USB_PATH_TA = (1 << 24),
	USB_PATH_CP = (1 << 20),
#if defined(CONFIG_MACH_P4NOTE)
	USB_PATH_AP = (1 << 16),
#else
	USB_PATH_OTG = (1 << 16),
	USB_PATH_HOST = (1 << 12)
#endif
};

extern int usb_switch_lock(void);
extern int usb_switch_trylock(void);
extern void usb_switch_unlock(void);

extern enum usb_path_t usb_switch_get_path(void);
extern void usb_switch_set_path(enum usb_path_t path);
extern void usb_switch_clr_path(enum usb_path_t path);

extern void set_usb_connection_state(bool connected);

#ifdef CONFIG_TARGET_LOCALE_KOR
extern int px_switch_get_usb_lock_state(void);
#endif

#endif
