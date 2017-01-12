/*
 *
 * (C) COPYRIGHT 2015-2016 ARM Limited. All rights reserved.
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


#ifndef _KBASE_GPU_ID_H_
#define _KBASE_GPU_ID_H_

/* GPU_ID register */
#define GPU_ID_VERSION_STATUS_SHIFT       0
#define GPU_ID_VERSION_MINOR_SHIFT        4
#define GPU_ID_VERSION_MAJOR_SHIFT        12
#define GPU_ID_VERSION_PRODUCT_ID_SHIFT   16
#define GPU_ID_VERSION_STATUS             (0xF  << GPU_ID_VERSION_STATUS_SHIFT)
#define GPU_ID_VERSION_MINOR              (0xFF << GPU_ID_VERSION_MINOR_SHIFT)
#define GPU_ID_VERSION_MAJOR              (0xF  << GPU_ID_VERSION_MAJOR_SHIFT)
#define GPU_ID_VERSION_PRODUCT_ID  (0xFFFF << GPU_ID_VERSION_PRODUCT_ID_SHIFT)

/* Values for GPU_ID_VERSION_PRODUCT_ID bitfield */
#define GPU_ID_PI_T60X                    0x6956
#define GPU_ID_PI_T62X                    0x0620
#define GPU_ID_PI_T76X                    0x0750
#define GPU_ID_PI_T72X                    0x0720
#define GPU_ID_PI_TFRX                    0x0880
#define GPU_ID_PI_T86X                    0x0860
#define GPU_ID_PI_T82X                    0x0820
#define GPU_ID_PI_T83X                    0x0830

/* New GPU ID format when PRODUCT_ID is >= 0x1000 (and not 0x6956) */
#define GPU_ID_PI_NEW_FORMAT_START        0x1000
#define GPU_ID_IS_NEW_FORMAT(product_id)  ((product_id) != GPU_ID_PI_T60X && \
						(product_id) >= \
						GPU_ID_PI_NEW_FORMAT_START)

#define GPU_ID2_VERSION_STATUS_SHIFT      0
#define GPU_ID2_VERSION_MINOR_SHIFT       4
#define GPU_ID2_VERSION_MAJOR_SHIFT       12
#define GPU_ID2_PRODUCT_MAJOR_SHIFT       16
#define GPU_ID2_ARCH_REV_SHIFT            20
#define GPU_ID2_ARCH_MINOR_SHIFT          24
#define GPU_ID2_ARCH_MAJOR_SHIFT          28
#define GPU_ID2_VERSION_STATUS            (0xF << GPU_ID2_VERSION_STATUS_SHIFT)
#define GPU_ID2_VERSION_MINOR             (0xFF << GPU_ID2_VERSION_MINOR_SHIFT)
#define GPU_ID2_VERSION_MAJOR             (0xF << GPU_ID2_VERSION_MAJOR_SHIFT)
#define GPU_ID2_PRODUCT_MAJOR             (0xF << GPU_ID2_PRODUCT_MAJOR_SHIFT)
#define GPU_ID2_ARCH_REV                  (0xF << GPU_ID2_ARCH_REV_SHIFT)
#define GPU_ID2_ARCH_MINOR                (0xF << GPU_ID2_ARCH_MINOR_SHIFT)
#define GPU_ID2_ARCH_MAJOR                (0xF << GPU_ID2_ARCH_MAJOR_SHIFT)
#define GPU_ID2_PRODUCT_MODEL  (GPU_ID2_ARCH_MAJOR | GPU_ID2_PRODUCT_MAJOR)

/* Helper macro to create a partial GPU_ID (new format) that defines
   a product ignoring its version. */
#define GPU_ID2_PRODUCT_MAKE(arch_major, arch_minor, arch_rev, product_major) \
		(((arch_major) << GPU_ID2_ARCH_MAJOR_SHIFT)  | \
		 ((arch_minor) << GPU_ID2_ARCH_MINOR_SHIFT)  | \
		 ((arch_rev) << GPU_ID2_ARCH_REV_SHIFT)      | \
		 ((product_major) << GPU_ID2_PRODUCT_MAJOR_SHIFT))

/* Helper macro to create a partial GPU_ID (new format) that specifies the
   revision (major, minor, status) of a product */
#define GPU_ID2_VERSION_MAKE(version_major, version_minor, version_status) \
		(((version_major) << GPU_ID2_VERSION_MAJOR_SHIFT)  | \
		 ((version_minor) << GPU_ID2_VERSION_MINOR_SHIFT)  | \
		 ((version_status) << GPU_ID2_VERSION_STATUS_SHIFT))

/* Helper macro to create a complete GPU_ID (new format) */
#define GPU_ID2_MAKE(arch_major, arch_minor, arch_rev, product_major, \
	version_major, version_minor, version_status) \
		(GPU_ID2_PRODUCT_MAKE(arch_major, arch_minor, arch_rev, \
			product_major) | \
		 GPU_ID2_VERSION_MAKE(version_major, version_minor,     \
			version_status))

/* Helper macro to create a partial GPU_ID (new format) that identifies
   a particular GPU model by its arch_major and product_major. */
#define GPU_ID2_MODEL_MAKE(arch_major, product_major) \
		(((arch_major) << GPU_ID2_ARCH_MAJOR_SHIFT)  | \
		((product_major) << GPU_ID2_PRODUCT_MAJOR_SHIFT))

/* Strip off the non-relevant bits from a product_id value and make it suitable
   for comparison against the GPU_ID2_PRODUCT_xxx values which identify a GPU
   model. */
#define GPU_ID2_MODEL_MATCH_VALUE(product_id) \
		(((product_id) << GPU_ID2_PRODUCT_MAJOR_SHIFT) & \
		    GPU_ID2_PRODUCT_MODEL)

#define GPU_ID2_PRODUCT_TMIX              GPU_ID2_MODEL_MAKE(6, 0)

/* Values for GPU_ID_VERSION_STATUS field for PRODUCT_ID GPU_ID_PI_T60X */
#define GPU_ID_S_15DEV0                   0x1
#define GPU_ID_S_EAC                      0x2

/* Helper macro to create a GPU_ID assuming valid values for id, major,
   minor, status */
#define GPU_ID_MAKE(id, major, minor, status) \
		(((id) << GPU_ID_VERSION_PRODUCT_ID_SHIFT) | \
		((major) << GPU_ID_VERSION_MAJOR_SHIFT) |   \
		((minor) << GPU_ID_VERSION_MINOR_SHIFT) |   \
		((status) << GPU_ID_VERSION_STATUS_SHIFT))

#endif /* _KBASE_GPU_ID_H_ */
