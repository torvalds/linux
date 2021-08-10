// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2017, 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/* Kernel UTF test helpers that mirror those for kutf-userside */
#include <kutf/kutf_helpers_user.h>
#include <kutf/kutf_helpers.h>
#include <kutf/kutf_utils.h>

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/export.h>

const char *valtype_names[] = {
	"INVALID",
	"U64",
	"STR",
};

static const char *get_val_type_name(enum kutf_helper_valtype valtype)
{
	/* enums can be signed or unsigned (implementation dependant), so
	 * enforce it to prevent:
	 * a) "<0 comparison on unsigned type" warning - if we did both upper
	 *    and lower bound check
	 * b) incorrect range checking if it was a signed type - if we did
	 *    upper bound check only
	 */
	unsigned int type_idx = (unsigned int)valtype;

	if (type_idx >= (unsigned int)KUTF_HELPER_VALTYPE_COUNT)
		type_idx = (unsigned int)KUTF_HELPER_VALTYPE_INVALID;

	return valtype_names[type_idx];
}

/* Check up to str_len chars of val_str to see if it's a valid value name:
 *
 * - Has between 1 and KUTF_HELPER_MAX_VAL_NAME_LEN characters before the \0 terminator
 * - And, each char is in the character set [A-Z0-9_]
 */
static int validate_val_name(const char *val_str, int str_len)
{
	int i = 0;

	for (i = 0; str_len && i <= KUTF_HELPER_MAX_VAL_NAME_LEN && val_str[i] != '\0'; ++i, --str_len) {
		char val_chr = val_str[i];

		if (val_chr >= 'A' && val_chr <= 'Z')
			continue;
		if (val_chr >= '0' && val_chr <= '9')
			continue;
		if (val_chr == '_')
			continue;

		/* Character not in the set [A-Z0-9_] - report error */
		return 1;
	}

	/* Names of 0 length are not valid */
	if (i == 0)
		return 1;
	/* Length greater than KUTF_HELPER_MAX_VAL_NAME_LEN not allowed */
	if (i > KUTF_HELPER_MAX_VAL_NAME_LEN || (i == KUTF_HELPER_MAX_VAL_NAME_LEN && val_str[i] != '\0'))
		return 1;

	return 0;
}

/* Find the length of the valid part of the string when it will be in quotes
 * e.g. "str"
 *
 * That is, before any '\\', '\n' or '"' characters. This is so we don't have
 * to escape the string
 */
static int find_quoted_string_valid_len(const char *str)
{
	char *ptr;
	const char *check_chars = "\\\n\"";

	ptr = strpbrk(str, check_chars);
	if (ptr)
		return (int)(ptr-str);

	return (int)strlen(str);
}

static int kutf_helper_userdata_enqueue(struct kutf_context *context,
		const char *str)
{
	char *str_copy;
	size_t len;
	int err;

	len = strlen(str)+1;

	str_copy = kutf_mempool_alloc(&context->fixture_pool, len);
	if (!str_copy)
		return -ENOMEM;

	strcpy(str_copy, str);

	err = kutf_add_result(context, KUTF_RESULT_USERDATA, str_copy);

	return err;
}

#define MAX_U64_HEX_LEN 16
/* (Name size) + ("=0x" size) + (64-bit hex value size) + (terminator) */
#define NAMED_U64_VAL_BUF_SZ (KUTF_HELPER_MAX_VAL_NAME_LEN + 3 + MAX_U64_HEX_LEN + 1)

int kutf_helper_send_named_u64(struct kutf_context *context,
		const char *val_name, u64 val)
{
	int ret = 1;
	char msgbuf[NAMED_U64_VAL_BUF_SZ];
	const char *errmsg = NULL;

	if (validate_val_name(val_name, KUTF_HELPER_MAX_VAL_NAME_LEN + 1)) {
		errmsg = kutf_dsprintf(&context->fixture_pool,
				"Failed to send u64 value named '%s': Invalid value name", val_name);
		goto out_err;
	}

	ret = snprintf(msgbuf, NAMED_U64_VAL_BUF_SZ, "%s=0x%llx", val_name, val);
	if (ret >= NAMED_U64_VAL_BUF_SZ || ret < 0) {
		errmsg = kutf_dsprintf(&context->fixture_pool,
				"Failed to send u64 value named '%s': snprintf() problem buffer size==%d ret=%d",
				val_name, NAMED_U64_VAL_BUF_SZ, ret);
		goto out_err;
	}

	ret = kutf_helper_userdata_enqueue(context, msgbuf);
	if (ret) {
		errmsg = kutf_dsprintf(&context->fixture_pool,
				"Failed to send u64 value named '%s': send returned %d",
				val_name, ret);
		goto out_err;
	}

	return ret;
out_err:
	kutf_test_fail(context, errmsg);
	return ret;
}
EXPORT_SYMBOL(kutf_helper_send_named_u64);

#define NAMED_VALUE_SEP "="
#define NAMED_STR_START_DELIM NAMED_VALUE_SEP "\""
#define NAMED_STR_END_DELIM "\""

int kutf_helper_max_str_len_for_kern(const char *val_name,
		int kern_buf_sz)
{
	const int val_name_len = strlen(val_name);
	const int start_delim_len = strlen(NAMED_STR_START_DELIM);
	const int end_delim_len = strlen(NAMED_STR_END_DELIM);
	int max_msg_len = kern_buf_sz;
	int max_str_len;

	max_str_len = max_msg_len - val_name_len - start_delim_len -
		end_delim_len;

	return max_str_len;
}
EXPORT_SYMBOL(kutf_helper_max_str_len_for_kern);

int kutf_helper_send_named_str(struct kutf_context *context,
		const char *val_name,
		const char *val_str)
{
	int val_str_len;
	int str_buf_sz;
	char *str_buf = NULL;
	int ret = 1;
	char *copy_ptr;
	int val_name_len;
	int start_delim_len = strlen(NAMED_STR_START_DELIM);
	int end_delim_len = strlen(NAMED_STR_END_DELIM);
	const char *errmsg = NULL;

	if (validate_val_name(val_name, KUTF_HELPER_MAX_VAL_NAME_LEN + 1)) {
		errmsg = kutf_dsprintf(&context->fixture_pool,
				"Failed to send u64 value named '%s': Invalid value name", val_name);
		goto out_err;
	}
	val_name_len = strlen(val_name);

	val_str_len = find_quoted_string_valid_len(val_str);

	/* (name length) + ("=\"" length) + (val_str len) + ("\"" length) + terminator */
	str_buf_sz = val_name_len + start_delim_len + val_str_len + end_delim_len + 1;

	/* Using kmalloc() here instead of mempool since we know we need to free
	 * before we return
	 */
	str_buf = kmalloc(str_buf_sz, GFP_KERNEL);
	if (!str_buf) {
		errmsg = kutf_dsprintf(&context->fixture_pool,
				"Failed to send str value named '%s': kmalloc failed, str_buf_sz=%d",
				val_name, str_buf_sz);
		goto out_err;
	}
	copy_ptr = str_buf;

	/* Manually copy each string component instead of snprintf because
	 * val_str may need to end early, and less error path handling
	 */

	/* name */
	memcpy(copy_ptr, val_name, val_name_len);
	copy_ptr += val_name_len;

	/* str start delimiter */
	memcpy(copy_ptr, NAMED_STR_START_DELIM, start_delim_len);
	copy_ptr += start_delim_len;

	/* str value */
	memcpy(copy_ptr, val_str, val_str_len);
	copy_ptr += val_str_len;

	/* str end delimiter */
	memcpy(copy_ptr, NAMED_STR_END_DELIM, end_delim_len);
	copy_ptr += end_delim_len;

	/* Terminator */
	*copy_ptr = '\0';

	ret = kutf_helper_userdata_enqueue(context, str_buf);

	if (ret) {
		errmsg = kutf_dsprintf(&context->fixture_pool,
				"Failed to send str value named '%s': send returned %d",
				val_name, ret);
		goto out_err;
	}

	kfree(str_buf);
	return ret;

out_err:
	kutf_test_fail(context, errmsg);
	kfree(str_buf);
	return ret;
}
EXPORT_SYMBOL(kutf_helper_send_named_str);

int kutf_helper_receive_named_val(
		struct kutf_context *context,
		struct kutf_helper_named_val *named_val)
{
	size_t recv_sz;
	char *recv_str;
	char *search_ptr;
	char *name_str = NULL;
	int name_len;
	int strval_len;
	enum kutf_helper_valtype type = KUTF_HELPER_VALTYPE_INVALID;
	char *strval = NULL;
	u64 u64val = 0;
	int err = KUTF_HELPER_ERR_INVALID_VALUE;

	recv_str = kutf_helper_input_dequeue(context, &recv_sz);
	if (!recv_str)
		return -EBUSY;
	else if (IS_ERR(recv_str))
		return PTR_ERR(recv_str);

	/* Find the '=', grab the name and validate it */
	search_ptr = strnchr(recv_str, recv_sz, NAMED_VALUE_SEP[0]);
	if (search_ptr) {
		name_len = search_ptr - recv_str;
		if (!validate_val_name(recv_str, name_len)) {
			/* no need to reallocate - just modify string in place */
			name_str = recv_str;
			name_str[name_len] = '\0';

			/* Move until after the '=' */
			recv_str += (name_len + 1);
			recv_sz -= (name_len + 1);
		}
	}
	if (!name_str) {
		pr_err("Invalid name part for received string '%s'\n",
				recv_str);
		return KUTF_HELPER_ERR_INVALID_NAME;
	}

	/* detect value type */
	if (*recv_str == NAMED_STR_START_DELIM[1]) {
		/* string delimiter start*/
		++recv_str;
		--recv_sz;

		/* Find end of string */
		search_ptr = strnchr(recv_str, recv_sz, NAMED_STR_END_DELIM[0]);
		if (search_ptr) {
			strval_len = search_ptr - recv_str;
			/* Validate the string to ensure it contains no quotes */
			if (strval_len == find_quoted_string_valid_len(recv_str)) {
				/* no need to reallocate - just modify string in place */
				strval = recv_str;
				strval[strval_len] = '\0';

				/* Move until after the end delimiter */
				recv_str += (strval_len + 1);
				recv_sz -= (strval_len + 1);
				type = KUTF_HELPER_VALTYPE_STR;
			} else {
				pr_err("String value contains invalid characters in rest of received string '%s'\n", recv_str);
				err = KUTF_HELPER_ERR_CHARS_AFTER_VAL;
			}
		} else {
			pr_err("End of string delimiter not found in rest of received string '%s'\n", recv_str);
			err = KUTF_HELPER_ERR_NO_END_DELIMITER;
		}
	} else {
		/* possibly a number value - strtoull will parse it */
		err = kstrtoull(recv_str, 0, &u64val);
		/* unlike userspace can't get an end ptr, but if kstrtoull()
		 * reads characters after the number it'll report -EINVAL
		 */
		if (!err) {
			int len_remain = strnlen(recv_str, recv_sz);

			type = KUTF_HELPER_VALTYPE_U64;
			recv_str += len_remain;
			recv_sz -= len_remain;
		} else {
			/* special case: not a number, report as such */
			pr_err("Rest of received string was not a numeric value or quoted string value: '%s'\n", recv_str);
		}
	}

	if (type == KUTF_HELPER_VALTYPE_INVALID)
		return err;

	/* Any remaining characters - error */
	if (strnlen(recv_str, recv_sz) != 0) {
		pr_err("Characters remain after value of type %s: '%s'\n",
				get_val_type_name(type), recv_str);
		return KUTF_HELPER_ERR_CHARS_AFTER_VAL;
	}

	/* Success - write into the output structure */
	switch (type) {
	case KUTF_HELPER_VALTYPE_U64:
		named_val->u.val_u64 = u64val;
		break;
	case KUTF_HELPER_VALTYPE_STR:
		named_val->u.val_str = strval;
		break;
	default:
		pr_err("Unreachable, fix kutf_helper_receive_named_val\n");
		/* Coding error, report as though 'run' file failed */
		return -EINVAL;
	}

	named_val->val_name = name_str;
	named_val->type = type;

	return KUTF_HELPER_ERR_NONE;
}
EXPORT_SYMBOL(kutf_helper_receive_named_val);

#define DUMMY_MSG "<placeholder due to test fail>"
int kutf_helper_receive_check_val(
		struct kutf_helper_named_val *named_val,
		struct kutf_context *context,
		const char *expect_val_name,
		enum kutf_helper_valtype expect_val_type)
{
	int err;

	err = kutf_helper_receive_named_val(context, named_val);
	if (err < 0) {
		const char *msg = kutf_dsprintf(&context->fixture_pool,
				"Failed to receive value named '%s'",
				expect_val_name);
		kutf_test_fail(context, msg);
		return err;
	} else if (err > 0) {
		const char *msg = kutf_dsprintf(&context->fixture_pool,
				"Named-value parse error when expecting value named '%s'",
				expect_val_name);
		kutf_test_fail(context, msg);
		goto out_fail_and_fixup;
	}

	if (named_val->val_name != NULL &&
			strcmp(named_val->val_name, expect_val_name) != 0) {
		const char *msg = kutf_dsprintf(&context->fixture_pool,
				"Expecting to receive value named '%s' but got '%s'",
				expect_val_name, named_val->val_name);
		kutf_test_fail(context, msg);
		goto out_fail_and_fixup;
	}


	if (named_val->type != expect_val_type) {
		const char *msg = kutf_dsprintf(&context->fixture_pool,
				"Expecting value named '%s' to be of type %s but got %s",
				expect_val_name, get_val_type_name(expect_val_type),
				get_val_type_name(named_val->type));
		kutf_test_fail(context, msg);
		goto out_fail_and_fixup;
	}

	return err;

out_fail_and_fixup:
	/* Produce a valid but incorrect value */
	switch (expect_val_type) {
	case KUTF_HELPER_VALTYPE_U64:
		named_val->u.val_u64 = 0ull;
		break;
	case KUTF_HELPER_VALTYPE_STR:
		{
			char *str = kutf_mempool_alloc(&context->fixture_pool, sizeof(DUMMY_MSG));

			if (!str)
				return -1;

			strcpy(str, DUMMY_MSG);
			named_val->u.val_str = str;
			break;
		}
	default:
		break;
	}

	/* Indicate that this is invalid */
	named_val->type = KUTF_HELPER_VALTYPE_INVALID;

	/* But at least allow the caller to continue in the test with failures */
	return 0;
}
EXPORT_SYMBOL(kutf_helper_receive_check_val);

void kutf_helper_output_named_val(struct kutf_helper_named_val *named_val)
{
	switch (named_val->type) {
	case KUTF_HELPER_VALTYPE_U64:
		pr_warn("%s=0x%llx\n", named_val->val_name, named_val->u.val_u64);
		break;
	case KUTF_HELPER_VALTYPE_STR:
		pr_warn("%s=\"%s\"\n", named_val->val_name, named_val->u.val_str);
		break;
	case KUTF_HELPER_VALTYPE_INVALID:
		pr_warn("%s is invalid\n", named_val->val_name);
		break;
	default:
		pr_warn("%s has unknown type %d\n", named_val->val_name, named_val->type);
		break;
	}
}
EXPORT_SYMBOL(kutf_helper_output_named_val);
