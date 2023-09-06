/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DMESG_DUMPER_PRIVATE_H
#define _DMESG_DUMPER_PRIVATE_H

#include <linux/dev_printk.h>
#include <linux/kmsg_dump.h>
#include <soc/qcom/minidump.h>

#define LOG_LINE_MAX				1024
#define IV_LEN						12
#define TAG_LEN						16
#define KEY_LEN						16
#define ALIGN_LEN					16
#define AES_256_ENCRYPTED_KEY_SIZE	256

/**
 * struct encrypt_data - the data struct of encrypted data
 * @frame_len: This encrypted frame size
 * @key: AES GCM encryption key wrapped up with OEM's public key
 * @iv: Nonce/IV used for current encryption
 * @tag: Tag used for validation
 * @cipher_log : The pointer to alive log with encrypt
 */
struct encrypt_data {
	u64 frame_size;
	u8 key[AES_256_ENCRYPTED_KEY_SIZE];
	u8 iv[IV_LEN];
	u8 tag[TAG_LEN];
	u8 cipher_log[0];
};

/**
 * struct ddump_shm_hdr - the header of shared memory
 * @user_buf_len: The userspace buffer size when PVM request
 * @svm_dump_len: The actual log length SVM dump
 * @svm_is_suspend: Indicate SVM is in suspend mode or not
 * @data: The data need by decrypt when enable encrypt and
 *        must be end of the hdr
 */
struct ddump_shm_hdr {
	u64 user_buf_len;
	u64 svm_dump_len;
	bool svm_is_suspend;
	struct encrypt_data data;
};

/**
 * struct qcom_dmesg_dumper - the qcom dmesg dumper driver data
 * @device: The qcom dmesg dumper device
 * @dump: Kernel crash message dumper
 * @iter: Iterator for retrieving kernel messages
 * @res: The shared memory resource
 * @base: The virtual address of shared memory
 * @size: The size of shared memory
 * @label, peer_name, memparcel: The info need by gunyah and
 *                               secure buffer driver
 * @primary_vm: Is primary virtual machine or not
 * @rm_nb: The resource manager callback
 * @tx_dbl: The gunyah doorbell tx handler
 * @rx_dbl: The gunyah doorbell rx handler
 * @ddump_completion: The completion for synchronization when dump
 *                    alive log
 * @wakeup_source : Avoid system enter suspend when dump alive log
 * @md_entry : minidump entry
 */
struct qcom_dmesg_dumper {
	struct device *dev;
	struct kmsg_dumper dump;
	struct kmsg_dump_iter iter;
	struct resource res;
	void *base;
	u64 size;
	u32 label, peer_name, memparcel;
	bool primary_vm;
	struct notifier_block rm_nb;
	void *tx_dbl;
	void *rx_dbl;
	struct completion ddump_completion;
	struct wakeup_source *wakeup_source;
	struct notifier_block gh_panic_nb;
	struct md_region md_entry;
	bool is_static;
};

#if IS_ENABLED(CONFIG_QCOM_VM_ALIVE_LOG_ENCRYPT)
#define DDUMP_GET_USER_HDR	(sizeof(struct encrypt_data) + TAG_LEN + ALIGN_LEN)
#define DDUMP_GET_SHM_HDR	(sizeof(struct ddump_shm_hdr) + TAG_LEN + ALIGN_LEN)

int qcom_ddump_encrypt_init(struct device_node *node);
void qcom_ddump_encrypt_exit(void);
int qcom_ddump_alive_log_to_shm(struct qcom_dmesg_dumper *qdd,
			     u64 user_size);
#else /* !CONFIG_QCOM_VM_ALIVE_LOG_ENCRYPT */
#define DDUMP_GET_USER_HDR	0
#define DDUMP_GET_SHM_HDR	offsetof(struct ddump_shm_hdr, data)

static inline int qcom_ddump_encrypt_init(struct device_node *node)
{
	return 0;
}

static inline void qcom_ddump_encrypt_exit(void)
{}
#endif /* CONFIG_QCOM_VM_ALIVE_LOG_ENCRYPT */

#if IS_ENABLED(CONFIG_ARCH_QTI_VM)
static inline u64 qcom_ddump_get_valid_size(struct qcom_dmesg_dumper *qdd,
			u64 user_size)
{
	u64 user_valid_size, shm_valid_size;
	u64 user_hdr_size, shm_hdr_size;

	user_hdr_size = DDUMP_GET_USER_HDR;
	shm_hdr_size = DDUMP_GET_SHM_HDR;

	if (user_size < user_hdr_size) {
		dev_err(qdd->dev, "user buffer size should greater than %d\n", user_hdr_size);
		return 0;
	}

	if (qdd->size < shm_hdr_size) {
		dev_err(qdd->dev, "Shared memory size should greater than %d\n", shm_hdr_size);
		return 0;
	}

	user_valid_size = user_size - user_hdr_size;
	shm_valid_size = qdd->size - shm_hdr_size;

	return min(user_valid_size, shm_valid_size);
}

#if !IS_ENABLED(CONFIG_QCOM_VM_ALIVE_LOG_ENCRYPT)
static int qcom_ddump_alive_log_to_shm(struct qcom_dmesg_dumper *qdd,
			     u64 user_size)
{
	size_t total_len, line_len;
	struct ddump_shm_hdr *hdr;
	u64 valid_size;
	void *base;

	total_len = 0;
	hdr = qdd->base;
	base = &hdr->data;

	valid_size = qcom_ddump_get_valid_size(qdd, user_size);
	if (valid_size < LOG_LINE_MAX)
		return -EINVAL;

	while ((total_len < valid_size - LOG_LINE_MAX) &&
		   kmsg_dump_get_line(&qdd->iter, false, base, valid_size - total_len, &line_len) &&
		   (line_len > 0)) {
		base = base + line_len;
		total_len = total_len + line_len;
	}

	hdr->svm_dump_len = total_len;
	return 0;
}
#endif /* !CONFIG_QCOM_VM_ALIVE_LOG_ENCRYPT */

#else /* !CONFIG_ARCH_QTI_VM */
static inline int qcom_ddump_alive_log_to_shm(struct qcom_dmesg_dumper *qdd,
			     u64 user_size)
{
	return 0;
}
#endif /* CONFIG_ARCH_QTI_VM */
#endif /* _DMESG_DUMPER_PRIVATE_H */
