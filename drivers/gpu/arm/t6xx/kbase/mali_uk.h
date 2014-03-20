/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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
 * @file mali_uk.h
 * Types and definitions that are common across OSs for both the user
 * and kernel side of the User-Kernel interface.
 */

#ifndef _UK_H_
#define _UK_H_

#ifdef __cplusplus
extern "C" {
#endif				/* __cplusplus */

#include <malisw/mali_stdtypes.h>

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @defgroup uk_api User-Kernel Interface API
 *
 * The User-Kernel Interface abstracts the communication mechanism between the user and kernel-side code of device
 * drivers developed as part of the Midgard DDK. Currently that includes the Base driver and the UMP driver.
 *
 * It exposes an OS independent API to user-side code (UKU) which routes functions calls to an OS-independent
 * kernel-side API (UKK) via an OS-specific communication mechanism.
 *
 * This API is internal to the Midgard DDK and is not exposed to any applications.
 *
 * @{
 */

/**
 * These are identifiers for kernel-side drivers implementing a UK interface, aka UKK clients. The
 * UK module maps this to an OS specific device name, e.g. "gpu_base" -> "GPU0:". Specify this
 * identifier to select a UKK client to the uku_open() function.
 *
 * When a new UKK client driver is created a new identifier needs to be added to the uk_client_id
 * enumeration and the uku_open() implemenation for the various OS ports need to be updated to
 * provide a mapping of the identifier to the OS specific device name.
 *
 */
	typedef enum uk_client_id {
	/**
	 * Value used to identify the Base driver UK client.
	 */
		UK_CLIENT_MALI_T600_BASE,

	/** The number of uk clients supported. This must be the last member of the enum */
		UK_CLIENT_COUNT
	} uk_client_id;

/**
 * Each function callable through the UK interface has a unique number.
 * Functions provided by UK clients start from number UK_FUNC_ID.
 * Numbers below UK_FUNC_ID are used for internal UK functions.
 */
	typedef enum uk_func {
		UKP_FUNC_ID_CHECK_VERSION,   /**< UKK Core internal function */
	/**
	 * Each UK client numbers the functions they provide starting from
	 * number UK_FUNC_ID. This number is then eventually assigned to the
	 * id field of the uk_header structure when preparing to make a
	 * UK call. See your UK client for a list of their function numbers.
	 */
		UK_FUNC_ID = 512
	} uk_func;

/**
 * Arguments for a UK call are stored in a structure. This structure consists
 * of a fixed size header and a payload. The header carries a 32-bit number
 * identifying the UK function to be called (see uk_func). When the UKK client
 * receives this header and executed the requested UK function, it will use
 * the same header to store the result of the function in the form of a
 * mali_error return code. The size of this structure is such that the
 * first member of the payload following the header can be accessed efficiently
 * on a 32 and 64-bit kernel and the structure has the same size regardless
 * of a 32 or 64-bit kernel. The uk_kernel_size_type type should be defined
 * accordingly in the OS specific mali_uk_os.h header file.
 */
	typedef union uk_header {
		/**
		 * 32-bit number identifying the UK function to be called.
		 * Also see uk_func.
		 */
		u32 id;
		/**
		 * The mali_error return code returned by the called UK function.
		 * See the specification of the particular UK function you are
		 * calling for the meaning of the error codes returned. All
		 * UK functions return MALI_ERROR_NONE on success.
		 */
		u32 ret;
		/*
		 * Used to ensure 64-bit alignment of this union. Do not remove.
		 * This field is used for padding and does not need to be initialized.
		 */
		u64 sizer;
	} uk_header;

/**
 * This structure carries a 16-bit major and minor number and is sent along with an internal UK call
 * used during uku_open to identify the versions of the UK module in use by the user-side and kernel-side.
 */
	typedef struct uku_version_check_args {
		uk_header header;
			  /**< UK call header */
		u16 major;
		   /**< This field carries the user-side major version on input and the kernel-side major version on output */
		u16 minor;
		   /**< This field carries the user-side minor version on input and the kernel-side minor version on output. */
		u8 padding[4];
	} uku_version_check_args;

/** @} end group uk_api */

	/** @} *//* end group base_api */

#ifdef __cplusplus
}
#endif				/* __cplusplus */
#endif				/* _UK_H_ */
