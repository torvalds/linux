/*
 *
 * (C) COPYRIGHT 2017 ARM Limited. All rights reserved.
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



#ifndef _KERNEL_UTF_HELPERS_USER_H_
#define _KERNEL_UTF_HELPERS_USER_H_

/* kutf_helpers.h
 * Test helper functions for the kernel UTF test infrastructure, whose
 * implementation mirrors that of similar functions for kutf-userside
 */

#include <kutf/kutf_suite.h>
#include <kutf/kutf_helpers.h>


#define KUTF_HELPER_MAX_VAL_NAME_LEN 255

enum kutf_helper_valtype {
	KUTF_HELPER_VALTYPE_INVALID,
	KUTF_HELPER_VALTYPE_U64,
	KUTF_HELPER_VALTYPE_STR,

	KUTF_HELPER_VALTYPE_COUNT /* Must be last */
};

struct kutf_helper_named_val {
	enum kutf_helper_valtype type;
	char *val_name;
	union {
		u64 val_u64;
		char *val_str;
	} u;
};

/* Extra error values for certain helpers when we want to distinguish between
 * Linux's own error values too.
 *
 * These can only be used on certain functions returning an int type that are
 * documented as returning one of these potential values, they cannot be used
 * from functions return a ptr type, since we can't decode it with PTR_ERR
 *
 * No negative values are used - Linux error codes should be used instead, and
 * indicate a problem in accessing the data file itself (are generally
 * unrecoverable)
 *
 * Positive values indicate correct access but invalid parsing (can be
 * recovered from assuming data in the future is correct) */
enum kutf_helper_err {
	/* No error - must be zero */
	KUTF_HELPER_ERR_NONE = 0,
	/* Named value parsing encountered an invalid name */
	KUTF_HELPER_ERR_INVALID_NAME,
	/* Named value parsing of string or u64 type encountered extra
	 * characters after the value (after the last digit for a u64 type or
	 * after the string end delimiter for string type) */
	KUTF_HELPER_ERR_CHARS_AFTER_VAL,
	/* Named value parsing of string type couldn't find the string end
	 * delimiter.
	 *
	 * This cannot be encountered when the NAME="value" message exceeds the
	 * textbuf's maximum line length, because such messages are not checked
	 * for an end string delimiter */
	KUTF_HELPER_ERR_NO_END_DELIMITER,
	/* Named value didn't parse as any of the known types */
	KUTF_HELPER_ERR_INVALID_VALUE,
};


/* textbuf Send named NAME=value pair, u64 value
 *
 * NAME must match [A-Z0-9_]\+ and can be up to MAX_VAL_NAME_LEN characters long
 *
 * This is assuming the kernel-side test is using the 'textbuf' helpers
 *
 * Any failure will be logged on the suite's current test fixture
 *
 * Returns 0 on success, non-zero on failure
 */
int kutf_helper_textbuf_send_named_u64(struct kutf_context *context,
		struct kutf_helper_textbuf *textbuf, char *val_name, u64 val);

/* Get the maximum length of a string that can be represented as a particular
 * NAME="value" pair without string-value truncation in the kernel's buffer
 *
 * Given val_name and the kernel buffer's size, this can be used to determine
 * the maximum length of a string that can be sent as val_name="value" pair
 * without having the string value truncated. Any string longer than this will
 * be truncated at some point during communication to this size.
 *
 * The calculation is valid both for sending strings of val_str_len to kernel,
 * and for receiving a string that was originally val_str_len from the kernel.
 *
 * It is assumed that valname is a valid name for
 * kutf_test_helpers_textbuf_send_named_str(), and no checking will be made to
 * ensure this.
 *
 * Returns the maximum string length that can be represented, or a negative
 * value if the NAME="value" encoding itself wouldn't fit in kern_buf_sz
 */
int kutf_helper_textbuf_max_str_len_for_kern(char *val_name, int kern_buf_sz);

/* textbuf Send named NAME="str" pair
 *
 * no escaping allowed in str. Any of the following characters will terminate
 * the string: '"' '\\' '\n'
 *
 * NAME must match [A-Z0-9_]\+ and can be up to MAX_VAL_NAME_LEN characters long
 *
 * This is assuming the kernel-side test is using the 'textbuf' helpers
 *
 * Any failure will be logged on the suite's current test fixture
 *
 * Returns 0 on success, non-zero on failure */
int kutf_helper_textbuf_send_named_str(struct kutf_context *context,
		struct kutf_helper_textbuf *textbuf, char *val_name,
		char *val_str);

/* textbuf Receive named NAME=value pair
 *
 * This can receive u64 and string values - check named_val->type
 *
 * If you are not planning on dynamic handling of the named value's name and
 * type, then kutf_test_helpers_textbuf_receive_check_val() is more useful as a
 * convenience function.
 *
 * String members of named_val will come from memory allocated on the fixture's mempool
 *
 * Returns 0 on success. Negative value on failure to receive from the 'data'
 * file, positive value indicates an enum kutf_helper_err value for correct
 * reception of data but invalid parsing */
int kutf_helper_textbuf_receive_named_val(struct kutf_helper_named_val *named_val,
		struct kutf_helper_textbuf *textbuf);

/* textbuf Receive and validate NAME=value pair
 *
 * As with kutf_test_helpers_textbuf_receive_named_val, but validate that the
 * name and type are as expected, as a convenience for a common pattern found
 * in tests.
 *
 * NOTE: this only returns an error value if there was actually a problem
 * receiving data.
 *
 * NOTE: If the underlying data was received correctly, but:
 * - isn't of the expected name
 * - isn't the expected type
 * - isn't correctly parsed for the type
 * then the following happens:
 * - failure result is recorded
 * - named_val->type will be KUTF_HELPER_VALTYPE_INVALID
 * - named_val->u will contain some default value that should be relatively
 *   harmless for the test, including being writable in the case of string
 *   values
 * - return value will be 0 to indicate success
 *
 * The rationale behind this is that we'd prefer to continue the rest of the
 * test with failures propagated, rather than hitting a timeout */
int kutf_helper_textbuf_receive_check_val(struct kutf_helper_named_val *named_val,
		struct kutf_context *context, struct kutf_helper_textbuf *textbuf,
		char *expect_val_name, enum kutf_helper_valtype expect_val_type);

/* Output a named value to kmsg */
void kutf_helper_output_named_val(struct kutf_helper_named_val *named_val);


#endif	/* _KERNEL_UTF_HELPERS_USER_H_ */
