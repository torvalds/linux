// SPDX-License-Identifier: GPL-2.0
/*
 * Handling of TPM command and other buffers.
 */

#include <linux/module.h>
#include <linux/tpm.h>

int tpm_buf_init(struct tpm_buf *buf, u16 tag, u32 ordinal)
{
	buf->data = (u8 *)__get_free_page(GFP_KERNEL);
	if (!buf->data)
		return -ENOMEM;

	buf->flags = 0;
	tpm_buf_reset(buf, tag, ordinal);
	return 0;
}
EXPORT_SYMBOL_GPL(tpm_buf_init);

void tpm_buf_reset(struct tpm_buf *buf, u16 tag, u32 ordinal)
{
	struct tpm_header *head = (struct tpm_header *)buf->data;

	head->tag = cpu_to_be16(tag);
	head->length = cpu_to_be32(sizeof(*head));
	head->ordinal = cpu_to_be32(ordinal);
}
EXPORT_SYMBOL_GPL(tpm_buf_reset);

void tpm_buf_destroy(struct tpm_buf *buf)
{
	free_page((unsigned long)buf->data);
}
EXPORT_SYMBOL_GPL(tpm_buf_destroy);

u32 tpm_buf_length(struct tpm_buf *buf)
{
	struct tpm_header *head = (struct tpm_header *)buf->data;

	return be32_to_cpu(head->length);
}
EXPORT_SYMBOL_GPL(tpm_buf_length);

void tpm_buf_append(struct tpm_buf *buf,
		    const unsigned char *new_data,
		    unsigned int new_len)
{
	struct tpm_header *head = (struct tpm_header *)buf->data;
	u32 len = tpm_buf_length(buf);

	/* Return silently if overflow has already happened. */
	if (buf->flags & TPM_BUF_OVERFLOW)
		return;

	if ((len + new_len) > PAGE_SIZE) {
		WARN(1, "tpm_buf: overflow\n");
		buf->flags |= TPM_BUF_OVERFLOW;
		return;
	}

	memcpy(&buf->data[len], new_data, new_len);
	head->length = cpu_to_be32(len + new_len);
}
EXPORT_SYMBOL_GPL(tpm_buf_append);

void tpm_buf_append_u8(struct tpm_buf *buf, const u8 value)
{
	tpm_buf_append(buf, &value, 1);
}
EXPORT_SYMBOL_GPL(tpm_buf_append_u8);

void tpm_buf_append_u16(struct tpm_buf *buf, const u16 value)
{
	__be16 value2 = cpu_to_be16(value);

	tpm_buf_append(buf, (u8 *)&value2, 2);
}
EXPORT_SYMBOL_GPL(tpm_buf_append_u16);

void tpm_buf_append_u32(struct tpm_buf *buf, const u32 value)
{
	__be32 value2 = cpu_to_be32(value);

	tpm_buf_append(buf, (u8 *)&value2, 4);
}
EXPORT_SYMBOL_GPL(tpm_buf_append_u32);
