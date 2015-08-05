/*
 *
 * (C) COPYRIGHT 2010-2014 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#ifdef CONFIG_MALI_PLATFORM_FAKE

/**
 * kbase_platform_fake_register - Entry point for fake platform registration
 *
 * This function is called early on in the initialization during execution of
 * kbase_driver_init.
 *
 * Return: 0 to indicate success, non-zero for failure.
 */
int kbase_platform_fake_register(void);

/**
 * kbase_platform_fake_unregister - Entry point for fake platform unregistration
 *
 * This function is called in the termination during execution of
 * kbase_driver_exit.
 */
void kbase_platform_fake_unregister(void);

#endif /* CONFIG_MALI_PLATFORM_FAKE */
