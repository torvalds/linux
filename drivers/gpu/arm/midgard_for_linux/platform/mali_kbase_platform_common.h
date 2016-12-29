/*
 *
 * (C) COPYRIGHT 2010-2013 ARM Limited. All rights reserved.
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



/**
 * @brief Entry point to transfer control to a platform for early initialization
 *
 * This function is called early on in the initialization during execution of
 * @ref kbase_driver_init.
 *
 * @return Zero to indicate success non-zero for failure.
 */
int kbase_platform_early_init(void);
