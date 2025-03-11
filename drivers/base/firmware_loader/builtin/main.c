// SPDX-License-Identifier: GPL-2.0
/* Builtin firmware support */

#include <linux/firmware.h>
#include "../firmware.h"

/* Only if FW_LOADER=y */
#ifdef CONFIG_FW_LOADER

struct builtin_fw {
	char *name;
	void *data;
	unsigned long size;
};

extern struct builtin_fw __start_builtin_fw[];
extern struct builtin_fw __end_builtin_fw[];

static bool fw_copy_to_prealloc_buf(struct firmware *fw,
				    void *buf, size_t size)
{
	if (!buf)
		return true;
	if (size < fw->size)
		return false;
	memcpy(buf, fw->data, fw->size);
	return true;
}

/**
 * firmware_request_builtin() - load builtin firmware
 * @fw: pointer to firmware struct
 * @name: name of firmware file
 *
 * Some use cases in the kernel have a requirement so that no memory allocator
 * is involved as these calls take place early in boot process. An example is
 * the x86 CPU microcode loader. In these cases all the caller wants is to see
 * if the firmware was built-in and if so use it right away. This can be used
 * for such cases.
 *
 * This looks for the firmware in the built-in kernel. Only if the kernel was
 * built-in with the firmware you are looking for will this return successfully.
 *
 * Callers of this API do not need to use release_firmware() as the pointer to
 * the firmware is expected to be provided locally on the stack of the caller.
 **/
bool firmware_request_builtin(struct firmware *fw, const char *name)
{
	struct builtin_fw *b_fw;

	if (!fw)
		return false;

	for (b_fw = __start_builtin_fw; b_fw != __end_builtin_fw; b_fw++) {
		if (strcmp(name, b_fw->name) == 0) {
			fw->size = b_fw->size;
			fw->data = b_fw->data;
			return true;
		}
	}

	return false;
}
EXPORT_SYMBOL_NS_GPL(firmware_request_builtin, "TEST_FIRMWARE");

/**
 * firmware_request_builtin_buf() - load builtin firmware into optional buffer
 * @fw: pointer to firmware struct
 * @name: name of firmware file
 * @buf: If set this lets you use a pre-allocated buffer so that the built-in
 *	firmware into is copied into. This field can be NULL. It is used by
 *	callers such as request_firmware_into_buf() and
 *	request_partial_firmware_into_buf()
 * @size: if buf was provided, the max size of the allocated buffer available.
 *	If the built-in firmware does not fit into the pre-allocated @buf this
 *	call will fail.
 *
 * This looks for the firmware in the built-in kernel. Only if the kernel was
 * built-in with the firmware you are looking for will this call possibly
 * succeed. If you passed a @buf the firmware will be copied into it *iff* the
 * built-in firmware fits into the pre-allocated buffer size specified in
 * @size.
 *
 * This caller is to be used internally by the firmware_loader only.
 **/
bool firmware_request_builtin_buf(struct firmware *fw, const char *name,
				  void *buf, size_t size)
{
	if (!firmware_request_builtin(fw, name))
		return false;

	return fw_copy_to_prealloc_buf(fw, buf, size);
}

bool firmware_is_builtin(const struct firmware *fw)
{
	struct builtin_fw *b_fw;

	for (b_fw = __start_builtin_fw; b_fw != __end_builtin_fw; b_fw++)
		if (fw->data == b_fw->data)
			return true;

	return false;
}

#endif
