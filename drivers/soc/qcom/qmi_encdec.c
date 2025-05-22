// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 */
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/soc/qcom/qmi.h>

#define QMI_ENCDEC_ENCODE_TLV(type, length, p_dst) do { \
	*p_dst++ = type; \
	*p_dst++ = ((u8)((length) & 0xFF)); \
	*p_dst++ = ((u8)(((length) >> 8) & 0xFF)); \
} while (0)

#define QMI_ENCDEC_DECODE_TLV(p_type, p_length, p_src) do { \
	*p_type = (u8)*p_src++; \
	*p_length = (u8)*p_src++; \
	*p_length |= ((u8)*p_src) << 8; \
} while (0)

#define QMI_ENCDEC_ENCODE_N_BYTES(p_dst, p_src, size) \
do { \
	memcpy(p_dst, p_src, size); \
	p_dst = (u8 *)p_dst + size; \
	p_src = (u8 *)p_src + size; \
} while (0)

#define QMI_ENCDEC_DECODE_N_BYTES(p_dst, p_src, size) \
do { \
	memcpy(p_dst, p_src, size); \
	p_dst = (u8 *)p_dst + size; \
	p_src = (u8 *)p_src + size; \
} while (0)

#define UPDATE_ENCODE_VARIABLES(temp_si, buf_dst, \
				encoded_bytes, tlv_len, encode_tlv, rc) \
do { \
	buf_dst = (u8 *)buf_dst + rc; \
	encoded_bytes += rc; \
	tlv_len += rc; \
	temp_si = temp_si + 1; \
	encode_tlv = 1; \
} while (0)

#define UPDATE_DECODE_VARIABLES(buf_src, decoded_bytes, rc) \
do { \
	buf_src = (u8 *)buf_src + rc; \
	decoded_bytes += rc; \
} while (0)

#define TLV_LEN_SIZE sizeof(u16)
#define TLV_TYPE_SIZE sizeof(u8)
#define OPTIONAL_TLV_TYPE_START 0x10

static int qmi_encode(const struct qmi_elem_info *ei_array, void *out_buf,
		      const void *in_c_struct, u32 out_buf_len,
		      int enc_level);

static int qmi_decode(const struct qmi_elem_info *ei_array, void *out_c_struct,
		      const void *in_buf, u32 in_buf_len, int dec_level);

/**
 * skip_to_next_elem() - Skip to next element in the structure to be encoded
 * @ei_array: Struct info describing the element to be skipped.
 * @level: Depth level of encoding/decoding to identify nested structures.
 *
 * This function is used while encoding optional elements. If the flag
 * corresponding to an optional element is not set, then encoding the
 * optional element can be skipped. This function can be used to perform
 * that operation.
 *
 * Return: struct info of the next element that can be encoded.
 */
static const struct qmi_elem_info *
skip_to_next_elem(const struct qmi_elem_info *ei_array, int level)
{
	const struct qmi_elem_info *temp_ei = ei_array;
	u8 tlv_type;

	if (level > 1) {
		temp_ei = temp_ei + 1;
	} else {
		do {
			tlv_type = temp_ei->tlv_type;
			temp_ei = temp_ei + 1;
		} while (tlv_type == temp_ei->tlv_type);
	}

	return temp_ei;
}

/**
 * qmi_calc_min_msg_len() - Calculate the minimum length of a QMI message
 * @ei_array: Struct info array describing the structure.
 * @level: Level to identify the depth of the nested structures.
 *
 * Return: Expected minimum length of the QMI message or 0 on error.
 */
static int qmi_calc_min_msg_len(const struct qmi_elem_info *ei_array,
				int level)
{
	int min_msg_len = 0;
	const struct qmi_elem_info *temp_ei = ei_array;

	if (!ei_array)
		return min_msg_len;

	while (temp_ei->data_type != QMI_EOTI) {
		/* Optional elements do not count in minimum length */
		if (temp_ei->data_type == QMI_OPT_FLAG) {
			temp_ei = skip_to_next_elem(temp_ei, level);
			continue;
		}

		if (temp_ei->data_type == QMI_DATA_LEN) {
			min_msg_len += (temp_ei->elem_size == sizeof(u8) ?
					sizeof(u8) : sizeof(u16));
			temp_ei++;
			continue;
		} else if (temp_ei->data_type == QMI_STRUCT) {
			min_msg_len += qmi_calc_min_msg_len(temp_ei->ei_array,
							    (level + 1));
			temp_ei++;
		} else if (temp_ei->data_type == QMI_STRING) {
			if (level > 1)
				min_msg_len += temp_ei->elem_len <= U8_MAX ?
					sizeof(u8) : sizeof(u16);
			min_msg_len += temp_ei->elem_len * temp_ei->elem_size;
			temp_ei++;
		} else {
			min_msg_len += (temp_ei->elem_len * temp_ei->elem_size);
			temp_ei++;
		}

		/*
		 * Type & Length info. not prepended for elements in the
		 * nested structure.
		 */
		if (level == 1)
			min_msg_len += (TLV_TYPE_SIZE + TLV_LEN_SIZE);
	}

	return min_msg_len;
}

/**
 * qmi_encode_basic_elem() - Encodes elements of basic/primary data type
 * @buf_dst: Buffer to store the encoded information.
 * @buf_src: Buffer containing the elements to be encoded.
 * @elem_len: Number of elements, in the buf_src, to be encoded.
 * @elem_size: Size of a single instance of the element to be encoded.
 *
 * This function encodes the "elem_len" number of data elements, each of
 * size "elem_size" bytes from the source buffer "buf_src" and stores the
 * encoded information in the destination buffer "buf_dst". The elements are
 * of primary data type which include u8 - u64 or similar. This
 * function returns the number of bytes of encoded information.
 *
 * Return: The number of bytes of encoded information.
 */
static int qmi_encode_basic_elem(void *buf_dst, const void *buf_src,
				 u32 elem_len, u32 elem_size)
{
	u32 i, rc = 0;

	for (i = 0; i < elem_len; i++) {
		QMI_ENCDEC_ENCODE_N_BYTES(buf_dst, buf_src, elem_size);
		rc += elem_size;
	}

	return rc;
}

/**
 * qmi_encode_struct_elem() - Encodes elements of struct data type
 * @ei_array: Struct info array descibing the struct element.
 * @buf_dst: Buffer to store the encoded information.
 * @buf_src: Buffer containing the elements to be encoded.
 * @elem_len: Number of elements, in the buf_src, to be encoded.
 * @out_buf_len: Available space in the encode buffer.
 * @enc_level: Depth of the nested structure from the main structure.
 *
 * This function encodes the "elem_len" number of struct elements, each of
 * size "ei_array->elem_size" bytes from the source buffer "buf_src" and
 * stores the encoded information in the destination buffer "buf_dst". The
 * elements are of struct data type which includes any C structure. This
 * function returns the number of bytes of encoded information.
 *
 * Return: The number of bytes of encoded information on success or negative
 * errno on error.
 */
static int qmi_encode_struct_elem(const struct qmi_elem_info *ei_array,
				  void *buf_dst, const void *buf_src,
				  u32 elem_len, u32 out_buf_len,
				  int enc_level)
{
	int i, rc, encoded_bytes = 0;
	const struct qmi_elem_info *temp_ei = ei_array;

	for (i = 0; i < elem_len; i++) {
		rc = qmi_encode(temp_ei->ei_array, buf_dst, buf_src,
				out_buf_len - encoded_bytes, enc_level);
		if (rc < 0) {
			pr_err("%s: STRUCT Encode failure\n", __func__);
			return rc;
		}
		buf_dst = buf_dst + rc;
		buf_src = buf_src + temp_ei->elem_size;
		encoded_bytes += rc;
	}

	return encoded_bytes;
}

/**
 * qmi_encode_string_elem() - Encodes elements of string data type
 * @ei_array: Struct info array descibing the string element.
 * @buf_dst: Buffer to store the encoded information.
 * @buf_src: Buffer containing the elements to be encoded.
 * @out_buf_len: Available space in the encode buffer.
 * @enc_level: Depth of the string element from the main structure.
 *
 * This function encodes a string element of maximum length "ei_array->elem_len"
 * bytes from the source buffer "buf_src" and stores the encoded information in
 * the destination buffer "buf_dst". This function returns the number of bytes
 * of encoded information.
 *
 * Return: The number of bytes of encoded information on success or negative
 * errno on error.
 */
static int qmi_encode_string_elem(const struct qmi_elem_info *ei_array,
				  void *buf_dst, const void *buf_src,
				  u32 out_buf_len, int enc_level)
{
	int rc;
	int encoded_bytes = 0;
	const struct qmi_elem_info *temp_ei = ei_array;
	u32 string_len = 0;
	u32 string_len_sz = 0;

	string_len = strlen(buf_src);
	string_len_sz = temp_ei->elem_len <= U8_MAX ?
			sizeof(u8) : sizeof(u16);
	if (string_len > temp_ei->elem_len) {
		pr_err("%s: String to be encoded is longer - %d > %d\n",
		       __func__, string_len, temp_ei->elem_len);
		return -EINVAL;
	}

	if (enc_level == 1) {
		if (string_len + TLV_LEN_SIZE + TLV_TYPE_SIZE >
		    out_buf_len) {
			pr_err("%s: Output len %d > Out Buf len %d\n",
			       __func__, string_len, out_buf_len);
			return -ETOOSMALL;
		}
	} else {
		if (string_len + string_len_sz > out_buf_len) {
			pr_err("%s: Output len %d > Out Buf len %d\n",
			       __func__, string_len, out_buf_len);
			return -ETOOSMALL;
		}
		rc = qmi_encode_basic_elem(buf_dst, &string_len,
					   1, string_len_sz);
		encoded_bytes += rc;
	}

	rc = qmi_encode_basic_elem(buf_dst + encoded_bytes, buf_src,
				   string_len, temp_ei->elem_size);
	encoded_bytes += rc;

	return encoded_bytes;
}

/**
 * qmi_encode() - Core Encode Function
 * @ei_array: Struct info array describing the structure to be encoded.
 * @out_buf: Buffer to hold the encoded QMI message.
 * @in_c_struct: Pointer to the C structure to be encoded.
 * @out_buf_len: Available space in the encode buffer.
 * @enc_level: Encode level to indicate the depth of the nested structure,
 *             within the main structure, being encoded.
 *
 * Return: The number of bytes of encoded information on success or negative
 * errno on error.
 */
static int qmi_encode(const struct qmi_elem_info *ei_array, void *out_buf,
		      const void *in_c_struct, u32 out_buf_len,
		      int enc_level)
{
	const struct qmi_elem_info *temp_ei = ei_array;
	u8 opt_flag_value = 0;
	u32 data_len_value = 0, data_len_sz;
	u8 *buf_dst = (u8 *)out_buf;
	u8 *tlv_pointer;
	u32 tlv_len;
	u8 tlv_type;
	u32 encoded_bytes = 0;
	const void *buf_src;
	int encode_tlv = 0;
	int rc;
	u8 val8;
	u16 val16;

	if (!ei_array)
		return 0;

	tlv_pointer = buf_dst;
	tlv_len = 0;
	if (enc_level == 1)
		buf_dst = buf_dst + (TLV_LEN_SIZE + TLV_TYPE_SIZE);

	while (temp_ei->data_type != QMI_EOTI) {
		buf_src = in_c_struct + temp_ei->offset;
		tlv_type = temp_ei->tlv_type;

		if (temp_ei->array_type == NO_ARRAY) {
			data_len_value = 1;
		} else if (temp_ei->array_type == STATIC_ARRAY) {
			data_len_value = temp_ei->elem_len;
		} else if (data_len_value <= 0 ||
			    temp_ei->elem_len < data_len_value) {
			pr_err("%s: Invalid data length\n", __func__);
			return -EINVAL;
		}

		switch (temp_ei->data_type) {
		case QMI_OPT_FLAG:
			rc = qmi_encode_basic_elem(&opt_flag_value, buf_src,
						   1, sizeof(u8));
			if (opt_flag_value)
				temp_ei = temp_ei + 1;
			else
				temp_ei = skip_to_next_elem(temp_ei, enc_level);
			break;

		case QMI_DATA_LEN:
			data_len_sz = temp_ei->elem_size == sizeof(u8) ?
					sizeof(u8) : sizeof(u16);
			/* Check to avoid out of range buffer access */
			if ((data_len_sz + encoded_bytes + TLV_LEN_SIZE +
			    TLV_TYPE_SIZE) > out_buf_len) {
				pr_err("%s: Too Small Buffer @DATA_LEN\n",
				       __func__);
				return -ETOOSMALL;
			}
			if (data_len_sz == sizeof(u8)) {
				val8 = *(u8 *)buf_src;
				data_len_value = (u32)val8;
				rc = qmi_encode_basic_elem(buf_dst, &val8,
							   1, data_len_sz);
			} else {
				val16 = *(u16 *)buf_src;
				data_len_value = (u32)le16_to_cpu(val16);
				rc = qmi_encode_basic_elem(buf_dst, &val16,
							   1, data_len_sz);
			}
			UPDATE_ENCODE_VARIABLES(temp_ei, buf_dst,
						encoded_bytes, tlv_len,
						encode_tlv, rc);
			if (!data_len_value)
				temp_ei = skip_to_next_elem(temp_ei, enc_level);
			else
				encode_tlv = 0;
			break;

		case QMI_UNSIGNED_1_BYTE:
		case QMI_UNSIGNED_2_BYTE:
		case QMI_UNSIGNED_4_BYTE:
		case QMI_UNSIGNED_8_BYTE:
		case QMI_SIGNED_2_BYTE_ENUM:
		case QMI_SIGNED_4_BYTE_ENUM:
			/* Check to avoid out of range buffer access */
			if (((data_len_value * temp_ei->elem_size) +
			    encoded_bytes + TLV_LEN_SIZE + TLV_TYPE_SIZE) >
			    out_buf_len) {
				pr_err("%s: Too Small Buffer @data_type:%d\n",
				       __func__, temp_ei->data_type);
				return -ETOOSMALL;
			}
			rc = qmi_encode_basic_elem(buf_dst, buf_src,
						   data_len_value,
						   temp_ei->elem_size);
			UPDATE_ENCODE_VARIABLES(temp_ei, buf_dst,
						encoded_bytes, tlv_len,
						encode_tlv, rc);
			break;

		case QMI_STRUCT:
			rc = qmi_encode_struct_elem(temp_ei, buf_dst, buf_src,
						    data_len_value,
						    out_buf_len - encoded_bytes,
						    enc_level + 1);
			if (rc < 0)
				return rc;
			UPDATE_ENCODE_VARIABLES(temp_ei, buf_dst,
						encoded_bytes, tlv_len,
						encode_tlv, rc);
			break;

		case QMI_STRING:
			rc = qmi_encode_string_elem(temp_ei, buf_dst, buf_src,
						    out_buf_len - encoded_bytes,
						    enc_level);
			if (rc < 0)
				return rc;
			UPDATE_ENCODE_VARIABLES(temp_ei, buf_dst,
						encoded_bytes, tlv_len,
						encode_tlv, rc);
			break;
		default:
			pr_err("%s: Unrecognized data type\n", __func__);
			return -EINVAL;
		}

		if (encode_tlv && enc_level == 1) {
			QMI_ENCDEC_ENCODE_TLV(tlv_type, tlv_len, tlv_pointer);
			encoded_bytes += (TLV_TYPE_SIZE + TLV_LEN_SIZE);
			tlv_pointer = buf_dst;
			tlv_len = 0;
			buf_dst = buf_dst + TLV_LEN_SIZE + TLV_TYPE_SIZE;
			encode_tlv = 0;
		}
	}

	return encoded_bytes;
}

/**
 * qmi_decode_basic_elem() - Decodes elements of basic/primary data type
 * @buf_dst: Buffer to store the decoded element.
 * @buf_src: Buffer containing the elements in QMI wire format.
 * @elem_len: Number of elements to be decoded.
 * @elem_size: Size of a single instance of the element to be decoded.
 *
 * This function decodes the "elem_len" number of elements in QMI wire format,
 * each of size "elem_size" bytes from the source buffer "buf_src" and stores
 * the decoded elements in the destination buffer "buf_dst". The elements are
 * of primary data type which include u8 - u64 or similar. This
 * function returns the number of bytes of decoded information.
 *
 * Return: The total size of the decoded data elements, in bytes.
 */
static int qmi_decode_basic_elem(void *buf_dst, const void *buf_src,
				 u32 elem_len, u32 elem_size)
{
	u32 i, rc = 0;

	for (i = 0; i < elem_len; i++) {
		QMI_ENCDEC_DECODE_N_BYTES(buf_dst, buf_src, elem_size);
		rc += elem_size;
	}

	return rc;
}

/**
 * qmi_decode_struct_elem() - Decodes elements of struct data type
 * @ei_array: Struct info array describing the struct element.
 * @buf_dst: Buffer to store the decoded element.
 * @buf_src: Buffer containing the elements in QMI wire format.
 * @elem_len: Number of elements to be decoded.
 * @tlv_len: Total size of the encoded information corresponding to
 *           this struct element.
 * @dec_level: Depth of the nested structure from the main structure.
 *
 * This function decodes the "elem_len" number of elements in QMI wire format,
 * each of size "(tlv_len/elem_len)" bytes from the source buffer "buf_src"
 * and stores the decoded elements in the destination buffer "buf_dst". The
 * elements are of struct data type which includes any C structure. This
 * function returns the number of bytes of decoded information.
 *
 * Return: The total size of the decoded data elements on success, negative
 * errno on error.
 */
static int qmi_decode_struct_elem(const struct qmi_elem_info *ei_array,
				  void *buf_dst, const void *buf_src,
				  u32 elem_len, u32 tlv_len,
				  int dec_level)
{
	int i, rc, decoded_bytes = 0;
	const struct qmi_elem_info *temp_ei = ei_array;

	for (i = 0; i < elem_len && decoded_bytes < tlv_len; i++) {
		rc = qmi_decode(temp_ei->ei_array, buf_dst, buf_src,
				tlv_len - decoded_bytes, dec_level);
		if (rc < 0)
			return rc;
		buf_src = buf_src + rc;
		buf_dst = buf_dst + temp_ei->elem_size;
		decoded_bytes += rc;
	}

	if ((dec_level <= 2 && decoded_bytes != tlv_len) ||
	    (dec_level > 2 && (i < elem_len || decoded_bytes > tlv_len))) {
		pr_err("%s: Fault in decoding: dl(%d), db(%d), tl(%d), i(%d), el(%d)\n",
		       __func__, dec_level, decoded_bytes, tlv_len,
		       i, elem_len);
		return -EFAULT;
	}

	return decoded_bytes;
}

/**
 * qmi_decode_string_elem() - Decodes elements of string data type
 * @ei_array: Struct info array describing the string element.
 * @buf_dst: Buffer to store the decoded element.
 * @buf_src: Buffer containing the elements in QMI wire format.
 * @tlv_len: Total size of the encoded information corresponding to
 *           this string element.
 * @dec_level: Depth of the string element from the main structure.
 *
 * This function decodes the string element of maximum length
 * "ei_array->elem_len" from the source buffer "buf_src" and puts it into
 * the destination buffer "buf_dst". This function returns number of bytes
 * decoded from the input buffer.
 *
 * Return: The total size of the decoded data elements on success, negative
 * errno on error.
 */
static int qmi_decode_string_elem(const struct qmi_elem_info *ei_array,
				  void *buf_dst, const void *buf_src,
				  u32 tlv_len, int dec_level)
{
	int rc;
	int decoded_bytes = 0;
	u32 string_len = 0;
	u32 string_len_sz = 0;
	const struct qmi_elem_info *temp_ei = ei_array;
	u8 val8;
	u16 val16;

	if (dec_level == 1) {
		string_len = tlv_len;
	} else {
		string_len_sz = temp_ei->elem_len <= U8_MAX ?
				sizeof(u8) : sizeof(u16);
		if (string_len_sz == sizeof(u8)) {
			rc = qmi_decode_basic_elem(&val8, buf_src,
						   1, string_len_sz);
			string_len = (u32)val8;
		} else {
			rc = qmi_decode_basic_elem(&val16, buf_src,
						   1, string_len_sz);
			string_len = (u32)val16;
		}
		decoded_bytes += rc;
	}

	if (string_len >= temp_ei->elem_len) {
		pr_err("%s: String len %d >= Max Len %d\n",
		       __func__, string_len, temp_ei->elem_len);
		return -ETOOSMALL;
	} else if (string_len > tlv_len) {
		pr_err("%s: String len %d > Input Buffer Len %d\n",
		       __func__, string_len, tlv_len);
		return -EFAULT;
	}

	rc = qmi_decode_basic_elem(buf_dst, buf_src + decoded_bytes,
				   string_len, temp_ei->elem_size);
	*((char *)buf_dst + string_len) = '\0';
	decoded_bytes += rc;

	return decoded_bytes;
}

/**
 * find_ei() - Find element info corresponding to TLV Type
 * @ei_array: Struct info array of the message being decoded.
 * @type: TLV Type of the element being searched.
 *
 * Every element that got encoded in the QMI message will have a type
 * information associated with it. While decoding the QMI message,
 * this function is used to find the struct info regarding the element
 * that corresponds to the type being decoded.
 *
 * Return: Pointer to struct info, if found
 */
static const struct qmi_elem_info *find_ei(const struct qmi_elem_info *ei_array,
					   u32 type)
{
	const struct qmi_elem_info *temp_ei = ei_array;

	while (temp_ei->data_type != QMI_EOTI) {
		if (temp_ei->tlv_type == (u8)type)
			return temp_ei;
		temp_ei = temp_ei + 1;
	}

	return NULL;
}

/**
 * qmi_decode() - Core Decode Function
 * @ei_array: Struct info array describing the structure to be decoded.
 * @out_c_struct: Buffer to hold the decoded C struct
 * @in_buf: Buffer containing the QMI message to be decoded
 * @in_buf_len: Length of the QMI message to be decoded
 * @dec_level: Decode level to indicate the depth of the nested structure,
 *             within the main structure, being decoded
 *
 * Return: The number of bytes of decoded information on success, negative
 * errno on error.
 */
static int qmi_decode(const struct qmi_elem_info *ei_array, void *out_c_struct,
		      const void *in_buf, u32 in_buf_len,
		      int dec_level)
{
	const struct qmi_elem_info *temp_ei = ei_array;
	u8 opt_flag_value = 1;
	u32 data_len_value = 0, data_len_sz = 0;
	u8 *buf_dst = out_c_struct;
	const u8 *tlv_pointer;
	u32 tlv_len = 0;
	u32 tlv_type;
	u32 decoded_bytes = 0;
	const void *buf_src = in_buf;
	int rc;
	u8 val8;
	u16 val16;
	u32 val32;

	while (decoded_bytes < in_buf_len) {
		if (dec_level >= 2 && temp_ei->data_type == QMI_EOTI)
			return decoded_bytes;

		if (dec_level == 1) {
			tlv_pointer = buf_src;
			QMI_ENCDEC_DECODE_TLV(&tlv_type,
					      &tlv_len, tlv_pointer);
			buf_src += (TLV_TYPE_SIZE + TLV_LEN_SIZE);
			decoded_bytes += (TLV_TYPE_SIZE + TLV_LEN_SIZE);
			temp_ei = find_ei(ei_array, tlv_type);
			if (!temp_ei && tlv_type < OPTIONAL_TLV_TYPE_START) {
				pr_err("%s: Inval element info\n", __func__);
				return -EINVAL;
			} else if (!temp_ei) {
				UPDATE_DECODE_VARIABLES(buf_src,
							decoded_bytes, tlv_len);
				continue;
			}
		} else {
			/*
			 * No length information for elements in nested
			 * structures. So use remaining decodable buffer space.
			 */
			tlv_len = in_buf_len - decoded_bytes;
		}

		buf_dst = out_c_struct + temp_ei->offset;
		if (temp_ei->data_type == QMI_OPT_FLAG) {
			memcpy(buf_dst, &opt_flag_value, sizeof(u8));
			temp_ei = temp_ei + 1;
			buf_dst = out_c_struct + temp_ei->offset;
		}

		if (temp_ei->data_type == QMI_DATA_LEN) {
			data_len_sz = temp_ei->elem_size == sizeof(u8) ?
					sizeof(u8) : sizeof(u16);
			if (data_len_sz == sizeof(u8)) {
				rc = qmi_decode_basic_elem(&val8, buf_src,
							   1, data_len_sz);
				data_len_value = (u32)val8;
			} else {
				rc = qmi_decode_basic_elem(&val16, buf_src,
							   1, data_len_sz);
				data_len_value = (u32)val16;
			}
			val32 = cpu_to_le32(data_len_value);
			memcpy(buf_dst, &val32, sizeof(u32));
			temp_ei = temp_ei + 1;
			buf_dst = out_c_struct + temp_ei->offset;
			tlv_len -= data_len_sz;
			UPDATE_DECODE_VARIABLES(buf_src, decoded_bytes, rc);
		}

		if (temp_ei->array_type == NO_ARRAY) {
			data_len_value = 1;
		} else if (temp_ei->array_type == STATIC_ARRAY) {
			data_len_value = temp_ei->elem_len;
		} else if (data_len_value > temp_ei->elem_len) {
			pr_err("%s: Data len %d > max spec %d\n",
			       __func__, data_len_value, temp_ei->elem_len);
			return -ETOOSMALL;
		}

		switch (temp_ei->data_type) {
		case QMI_UNSIGNED_1_BYTE:
		case QMI_UNSIGNED_2_BYTE:
		case QMI_UNSIGNED_4_BYTE:
		case QMI_UNSIGNED_8_BYTE:
		case QMI_SIGNED_2_BYTE_ENUM:
		case QMI_SIGNED_4_BYTE_ENUM:
			rc = qmi_decode_basic_elem(buf_dst, buf_src,
						   data_len_value,
						   temp_ei->elem_size);
			UPDATE_DECODE_VARIABLES(buf_src, decoded_bytes, rc);
			break;

		case QMI_STRUCT:
			rc = qmi_decode_struct_elem(temp_ei, buf_dst, buf_src,
						    data_len_value, tlv_len,
						    dec_level + 1);
			if (rc < 0)
				return rc;
			UPDATE_DECODE_VARIABLES(buf_src, decoded_bytes, rc);
			break;

		case QMI_STRING:
			rc = qmi_decode_string_elem(temp_ei, buf_dst, buf_src,
						    tlv_len, dec_level);
			if (rc < 0)
				return rc;
			UPDATE_DECODE_VARIABLES(buf_src, decoded_bytes, rc);
			break;

		default:
			pr_err("%s: Unrecognized data type\n", __func__);
			return -EINVAL;
		}
		temp_ei = temp_ei + 1;
	}

	return decoded_bytes;
}

/**
 * qmi_encode_message() - Encode C structure as QMI encoded message
 * @type:	Type of QMI message
 * @msg_id:	Message ID of the message
 * @len:	Passed as max length of the message, updated to actual size
 * @txn_id:	Transaction ID
 * @ei:		QMI message descriptor
 * @c_struct:	Reference to structure to encode
 *
 * Return: Buffer with encoded message, or negative ERR_PTR() on error
 */
void *qmi_encode_message(int type, unsigned int msg_id, size_t *len,
			 unsigned int txn_id, const struct qmi_elem_info *ei,
			 const void *c_struct)
{
	struct qmi_header *hdr;
	ssize_t msglen = 0;
	void *msg;
	int ret;

	/* Check the possibility of a zero length QMI message */
	if (!c_struct) {
		ret = qmi_calc_min_msg_len(ei, 1);
		if (ret) {
			pr_err("%s: Calc. len %d != 0, but NULL c_struct\n",
			       __func__, ret);
			return ERR_PTR(-EINVAL);
		}
	}

	msg = kzalloc(sizeof(*hdr) + *len, GFP_KERNEL);
	if (!msg)
		return ERR_PTR(-ENOMEM);

	/* Encode message, if we have a message */
	if (c_struct) {
		msglen = qmi_encode(ei, msg + sizeof(*hdr), c_struct, *len, 1);
		if (msglen < 0) {
			kfree(msg);
			return ERR_PTR(msglen);
		}
	}

	hdr = msg;
	hdr->type = type;
	hdr->txn_id = txn_id;
	hdr->msg_id = msg_id;
	hdr->msg_len = msglen;

	*len = sizeof(*hdr) + msglen;

	return msg;
}
EXPORT_SYMBOL_GPL(qmi_encode_message);

/**
 * qmi_decode_message() - Decode QMI encoded message to C structure
 * @buf:	Buffer with encoded message
 * @len:	Amount of data in @buf
 * @ei:		QMI message descriptor
 * @c_struct:	Reference to structure to decode into
 *
 * Return: The number of bytes of decoded information on success, negative
 * errno on error.
 */
int qmi_decode_message(const void *buf, size_t len,
		       const struct qmi_elem_info *ei, void *c_struct)
{
	if (!ei)
		return -EINVAL;

	if (!c_struct || !buf || !len)
		return -EINVAL;

	return qmi_decode(ei, c_struct, buf + sizeof(struct qmi_header),
			  len - sizeof(struct qmi_header), 1);
}
EXPORT_SYMBOL_GPL(qmi_decode_message);

/* Common header in all QMI responses */
const struct qmi_elem_info qmi_response_type_v01_ei[] = {
	{
		.data_type	= QMI_SIGNED_2_BYTE_ENUM,
		.elem_len	= 1,
		.elem_size	= sizeof(u16),
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
		.offset		= offsetof(struct qmi_response_type_v01, result),
		.ei_array	= NULL,
	},
	{
		.data_type	= QMI_SIGNED_2_BYTE_ENUM,
		.elem_len	= 1,
		.elem_size	= sizeof(u16),
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
		.offset		= offsetof(struct qmi_response_type_v01, error),
		.ei_array	= NULL,
	},
	{
		.data_type	= QMI_EOTI,
		.elem_len	= 0,
		.elem_size	= 0,
		.array_type	= NO_ARRAY,
		.tlv_type	= QMI_COMMON_TLV_TYPE,
		.offset		= 0,
		.ei_array	= NULL,
	},
};
EXPORT_SYMBOL_GPL(qmi_response_type_v01_ei);

MODULE_DESCRIPTION("QMI encoder/decoder helper");
MODULE_LICENSE("GPL v2");
