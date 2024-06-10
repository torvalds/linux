/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright 2018-2023 Broadcom Inc. All rights reserved.
 */
#ifndef MPI30_IMAGE_H
#define MPI30_IMAGE_H     1
struct mpi3_comp_image_version {
	__le16     build_num;
	__le16     customer_id;
	u8         phase_minor;
	u8         phase_major;
	u8         gen_minor;
	u8         gen_major;
};

struct mpi3_hash_exclusion_format {
	__le32                     offset;
	__le32                     size;
};

#define MPI3_IMAGE_HASH_EXCUSION_NUM                           (4)
struct mpi3_component_image_header {
	__le32                            signature0;
	__le32                            load_address;
	__le32                            data_size;
	__le32                            start_offset;
	__le32                            signature1;
	__le32                            flash_offset;
	__le32                            image_size;
	__le32                            version_string_offset;
	__le32                            build_date_string_offset;
	__le32                            build_time_string_offset;
	__le32                            environment_variable_offset;
	__le32                            application_specific;
	__le32                            signature2;
	__le32                            header_size;
	__le32                            crc;
	__le32                            flags;
	__le32                            secondary_flash_offset;
	__le32                            etp_offset;
	__le32                            etp_size;
	union mpi3_version_union             rmc_interface_version;
	union mpi3_version_union             etp_interface_version;
	struct mpi3_comp_image_version        component_image_version;
	struct mpi3_hash_exclusion_format     hash_exclusion[MPI3_IMAGE_HASH_EXCUSION_NUM];
	__le32                            next_image_header_offset;
	union mpi3_version_union             security_version;
	__le32                            reserved84[31];
};

#define MPI3_IMAGE_HEADER_SIGNATURE0_MPI3                     (0xeb00003e)
#define MPI3_IMAGE_HEADER_LOAD_ADDRESS_INVALID                (0x00000000)
#define MPI3_IMAGE_HEADER_SIGNATURE1_APPLICATION              (0x20505041)
#define MPI3_IMAGE_HEADER_SIGNATURE1_FIRST_MUTABLE            (0x20434d46)
#define MPI3_IMAGE_HEADER_SIGNATURE1_BSP                      (0x20505342)
#define MPI3_IMAGE_HEADER_SIGNATURE1_ROM_BIOS                 (0x534f4942)
#define MPI3_IMAGE_HEADER_SIGNATURE1_HII_X64                  (0x4d494948)
#define MPI3_IMAGE_HEADER_SIGNATURE1_HII_ARM                  (0x41494948)
#define MPI3_IMAGE_HEADER_SIGNATURE1_CPLD                     (0x444c5043)
#define MPI3_IMAGE_HEADER_SIGNATURE1_SPD                      (0x20445053)
#define MPI3_IMAGE_HEADER_SIGNATURE1_GAS_GAUGE                (0x20534147)
#define MPI3_IMAGE_HEADER_SIGNATURE1_PBLP                     (0x504c4250)
#define MPI3_IMAGE_HEADER_SIGNATURE1_MANIFEST                 (0x464e414d)
#define MPI3_IMAGE_HEADER_SIGNATURE1_OEM                      (0x204d454f)
#define MPI3_IMAGE_HEADER_SIGNATURE1_RMC                      (0x20434d52)
#define MPI3_IMAGE_HEADER_SIGNATURE1_SMM                      (0x204d4d53)
#define MPI3_IMAGE_HEADER_SIGNATURE1_PSW                      (0x20575350)
#define MPI3_IMAGE_HEADER_SIGNATURE2_VALUE                    (0x50584546)
#define MPI3_IMAGE_HEADER_FLAGS_DEVICE_KEY_BASIS_MASK         (0x00000030)
#define MPI3_IMAGE_HEADER_FLAGS_DEVICE_KEY_BASIS_CDI          (0x00000000)
#define MPI3_IMAGE_HEADER_FLAGS_DEVICE_KEY_BASIS_DI           (0x00000010)
#define MPI3_IMAGE_HEADER_FLAGS_SIGNED_NVDATA                 (0x00000008)
#define MPI3_IMAGE_HEADER_FLAGS_REQUIRES_ACTIVATION           (0x00000004)
#define MPI3_IMAGE_HEADER_FLAGS_COMPRESSED                    (0x00000002)
#define MPI3_IMAGE_HEADER_FLAGS_FLASH                         (0x00000001)
#define MPI3_IMAGE_HEADER_SIGNATURE0_OFFSET                   (0x00)
#define MPI3_IMAGE_HEADER_LOAD_ADDRESS_OFFSET                 (0x04)
#define MPI3_IMAGE_HEADER_DATA_SIZE_OFFSET                    (0x08)
#define MPI3_IMAGE_HEADER_START_OFFSET_OFFSET                 (0x0c)
#define MPI3_IMAGE_HEADER_SIGNATURE1_OFFSET                   (0x10)
#define MPI3_IMAGE_HEADER_FLASH_OFFSET_OFFSET                 (0x14)
#define MPI3_IMAGE_HEADER_FLASH_SIZE_OFFSET                   (0x18)
#define MPI3_IMAGE_HEADER_VERSION_STRING_OFFSET_OFFSET        (0x1c)
#define MPI3_IMAGE_HEADER_BUILD_DATE_STRING_OFFSET_OFFSET     (0x20)
#define MPI3_IMAGE_HEADER_BUILD_TIME_OFFSET_OFFSET            (0x24)
#define MPI3_IMAGE_HEADER_ENVIROMENT_VAR_OFFSET_OFFSET        (0x28)
#define MPI3_IMAGE_HEADER_APPLICATION_SPECIFIC_OFFSET         (0x2c)
#define MPI3_IMAGE_HEADER_SIGNATURE2_OFFSET                   (0x30)
#define MPI3_IMAGE_HEADER_HEADER_SIZE_OFFSET                  (0x34)
#define MPI3_IMAGE_HEADER_CRC_OFFSET                          (0x38)
#define MPI3_IMAGE_HEADER_FLAGS_OFFSET                        (0x3c)
#define MPI3_IMAGE_HEADER_SECONDARY_FLASH_OFFSET_OFFSET       (0x40)
#define MPI3_IMAGE_HEADER_ETP_OFFSET_OFFSET                   (0x44)
#define MPI3_IMAGE_HEADER_ETP_SIZE_OFFSET                     (0x48)
#define MPI3_IMAGE_HEADER_RMC_INTERFACE_VER_OFFSET            (0x4c)
#define MPI3_IMAGE_HEADER_ETP_INTERFACE_VER_OFFSET            (0x50)
#define MPI3_IMAGE_HEADER_COMPONENT_IMAGE_VER_OFFSET          (0x54)
#define MPI3_IMAGE_HEADER_HASH_EXCLUSION_OFFSET               (0x5c)
#define MPI3_IMAGE_HEADER_NEXT_IMAGE_HEADER_OFFSET_OFFSET     (0x7c)
#define MPI3_IMAGE_HEADER_SIZE                                (0x100)
#ifndef MPI3_CI_MANIFEST_MPI_MAX
#define MPI3_CI_MANIFEST_MPI_MAX                               (1)
#endif
struct mpi3_ci_manifest_mpi_comp_image_ref {
	__le32                                signature1;
	__le32                                reserved04[3];
	struct mpi3_comp_image_version            component_image_version;
	__le32                                component_image_version_string_offset;
	__le32                                crc;
};

struct mpi3_ci_manifest_mpi {
	u8                                       manifest_type;
	u8                                       reserved01[3];
	__le32                                   reserved04[3];
	u8                                       num_image_references;
	u8                                       release_level;
	__le16                                   reserved12;
	__le16                                   reserved14;
	__le16                                   flags;
	__le32                                   reserved18[2];
	__le16                                   vendor_id;
	__le16                                   device_id;
	__le16                                   subsystem_vendor_id;
	__le16                                   subsystem_id;
	__le32                                   reserved28[2];
	union mpi3_version_union                    package_security_version;
	__le32                                   reserved34;
	struct mpi3_comp_image_version               package_version;
	__le32                                   package_version_string_offset;
	__le32                                   package_build_date_string_offset;
	__le32                                   package_build_time_string_offset;
	__le32                                   reserved4c;
	__le32                                   diag_authorization_identifier[16];
	struct mpi3_ci_manifest_mpi_comp_image_ref   component_image_ref[MPI3_CI_MANIFEST_MPI_MAX];
};

#define MPI3_CI_MANIFEST_MPI_RELEASE_LEVEL_DEV                        (0x00)
#define MPI3_CI_MANIFEST_MPI_RELEASE_LEVEL_PREALPHA                   (0x10)
#define MPI3_CI_MANIFEST_MPI_RELEASE_LEVEL_ALPHA                      (0x20)
#define MPI3_CI_MANIFEST_MPI_RELEASE_LEVEL_BETA                       (0x30)
#define MPI3_CI_MANIFEST_MPI_RELEASE_LEVEL_RC                         (0x40)
#define MPI3_CI_MANIFEST_MPI_RELEASE_LEVEL_GCA                        (0x50)
#define MPI3_CI_MANIFEST_MPI_RELEASE_LEVEL_POINT                      (0x60)
#define MPI3_CI_MANIFEST_MPI_FLAGS_DIAG_AUTHORIZATION                 (0x01)
#define MPI3_CI_MANIFEST_MPI_SUBSYSTEMID_IGNORED                   (0xffff)
#define MPI3_CI_MANIFEST_MPI_PKG_VER_STR_OFF_UNSPECIFIED           (0x00000000)
#define MPI3_CI_MANIFEST_MPI_PKG_BUILD_DATE_STR_OFF_UNSPECIFIED    (0x00000000)
#define MPI3_CI_MANIFEST_MPI_PKG_BUILD_TIME_STR_OFF_UNSPECIFIED    (0x00000000)
union mpi3_ci_manifest {
	struct mpi3_ci_manifest_mpi               mpi;
	__le32                                dword[1];
};

#define MPI3_CI_MANIFEST_TYPE_MPI                                  (0x00)
struct mpi3_extended_image_header {
	u8                                image_type;
	u8                                reserved01[3];
	__le32                            checksum;
	__le32                            image_size;
	__le32                            next_image_header_offset;
	__le32                            reserved10[4];
	__le32                            identify_string[8];
};

#define MPI3_EXT_IMAGE_IMAGETYPE_OFFSET         (0x00)
#define MPI3_EXT_IMAGE_IMAGESIZE_OFFSET         (0x08)
#define MPI3_EXT_IMAGE_NEXTIMAGE_OFFSET         (0x0c)
#define MPI3_EXT_IMAGE_HEADER_SIZE              (0x40)
#define MPI3_EXT_IMAGE_TYPE_UNSPECIFIED             (0x00)
#define MPI3_EXT_IMAGE_TYPE_NVDATA                  (0x03)
#define MPI3_EXT_IMAGE_TYPE_SUPPORTED_DEVICES       (0x07)
#define MPI3_EXT_IMAGE_TYPE_ENCRYPTED_HASH          (0x09)
#define MPI3_EXT_IMAGE_TYPE_RDE                     (0x0a)
#define MPI3_EXT_IMAGE_TYPE_AUXILIARY_PROCESSOR     (0x0b)
#define MPI3_EXT_IMAGE_TYPE_MIN_PRODUCT_SPECIFIC    (0x80)
#define MPI3_EXT_IMAGE_TYPE_MAX_PRODUCT_SPECIFIC    (0xff)
struct mpi3_supported_device {
	__le16                     device_id;
	__le16                     vendor_id;
	__le16                     device_id_mask;
	__le16                     reserved06;
	u8                         low_pci_rev;
	u8                         high_pci_rev;
	__le16                     reserved0a;
	__le32                     reserved0c;
};

#ifndef MPI3_SUPPORTED_DEVICE_MAX
#define MPI3_SUPPORTED_DEVICE_MAX                      (1)
#endif
struct mpi3_supported_devices_data {
	u8                         image_version;
	u8                         reserved01;
	u8                         num_devices;
	u8                         reserved03;
	__le32                     reserved04;
	struct mpi3_supported_device   supported_device[MPI3_SUPPORTED_DEVICE_MAX];
};

#ifndef MPI3_PUBLIC_KEY_MAX
#define MPI3_PUBLIC_KEY_MAX                      (1)
#endif
struct mpi3_encrypted_hash_entry {
	u8                         hash_image_type;
	u8                         hash_algorithm;
	u8                         encryption_algorithm;
	u8                         reserved03;
	__le16                     public_key_size;
	__le16                     signature_size;
	__le32                     public_key[MPI3_PUBLIC_KEY_MAX];
};

#define MPI3_HASH_IMAGE_TYPE_KEY_WITH_SIGNATURE      (0x03)
#define MPI3_HASH_ALGORITHM_VERSION_MASK             (0xe0)
#define MPI3_HASH_ALGORITHM_VERSION_NONE             (0x00)
#define MPI3_HASH_ALGORITHM_VERSION_SHA1             (0x20)
#define MPI3_HASH_ALGORITHM_VERSION_SHA2             (0x40)
#define MPI3_HASH_ALGORITHM_VERSION_SHA3             (0x60)
#define MPI3_HASH_ALGORITHM_SIZE_MASK                (0x1f)
#define MPI3_HASH_ALGORITHM_SIZE_UNUSED              (0x00)
#define MPI3_HASH_ALGORITHM_SIZE_SHA256              (0x01)
#define MPI3_HASH_ALGORITHM_SIZE_SHA512              (0x02)
#define MPI3_HASH_ALGORITHM_SIZE_SHA384              (0x03)
#define MPI3_ENCRYPTION_ALGORITHM_UNUSED             (0x00)
#define MPI3_ENCRYPTION_ALGORITHM_RSA256             (0x01)
#define MPI3_ENCRYPTION_ALGORITHM_RSA512             (0x02)
#define MPI3_ENCRYPTION_ALGORITHM_RSA1024            (0x03)
#define MPI3_ENCRYPTION_ALGORITHM_RSA2048            (0x04)
#define MPI3_ENCRYPTION_ALGORITHM_RSA4096            (0x05)
#define MPI3_ENCRYPTION_ALGORITHM_RSA3072            (0x06)

#ifndef MPI3_ENCRYPTED_HASH_ENTRY_MAX
#define MPI3_ENCRYPTED_HASH_ENTRY_MAX               (1)
#endif
struct mpi3_encrypted_hash_data {
	u8                                  image_version;
	u8                                  num_hash;
	__le16                              reserved02;
	__le32                              reserved04;
	struct mpi3_encrypted_hash_entry        encrypted_hash_entry[MPI3_ENCRYPTED_HASH_ENTRY_MAX];
};

#ifndef MPI3_AUX_PROC_DATA_MAX
#define MPI3_AUX_PROC_DATA_MAX               (1)
#endif
struct mpi3_aux_processor_data {
	u8                         boot_method;
	u8                         num_load_addr;
	u8                         reserved02;
	u8                         type;
	__le32                     version;
	__le32                     load_address[8];
	__le32                     reserved28[22];
	__le32                     aux_processor_data[MPI3_AUX_PROC_DATA_MAX];
};

#define MPI3_AUX_PROC_DATA_OFFSET                                     (0x80)
#define MPI3_AUXPROCESSOR_BOOT_METHOD_MO_MSG                          (0x00)
#define MPI3_AUXPROCESSOR_BOOT_METHOD_MO_DOORBELL                     (0x01)
#define MPI3_AUXPROCESSOR_BOOT_METHOD_COMPONENT                       (0x02)
#define MPI3_AUXPROCESSOR_TYPE_ARM_A15                                (0x00)
#define MPI3_AUXPROCESSOR_TYPE_ARM_M0                                 (0x01)
#define MPI3_AUXPROCESSOR_TYPE_ARM_R4                                 (0x02)
#endif
