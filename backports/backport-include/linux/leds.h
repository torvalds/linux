#ifndef __BACKPORT_LINUX_LEDS_H
#define __BACKPORT_LINUX_LEDS_H
#include_next <linux/leds.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0)
/*
 * Backports 
 * 
 * commit 959d62fa865d2e616b61a509e1cc5b88741f065e
 * Author: Shuah Khan <shuahkhan@gmail.com>
 * Date:   Thu Jun 14 04:34:30 2012 +0800
 *
 *   leds: Rename led_brightness_set() to led_set_brightness()
 *   
 *   Rename leds external interface led_brightness_set() to led_set_brightness().
 *   This is the second phase of the change to reduce confusion between the
 *   leds internal and external interfaces that set brightness. With this change,
 *   now the external interface is led_set_brightness(). The first phase renamed
 *   the internal interface led_set_brightness() to __led_set_brightness().
 *   There are no changes to the interface implementations.
 *   
 *   Signed-off-by: Shuah Khan <shuahkhan@gmail.com>
 *   Signed-off-by: Bryan Wu <bryan.wu@canonical.com>
 */
#define led_set_brightness(_dev, _switch) led_brightness_set(_dev, _switch)
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0) */

#include <backport/leds-disabled.h>

#endif /* __BACKPORT_LINUX_LEDS_H */
