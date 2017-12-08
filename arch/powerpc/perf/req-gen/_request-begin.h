/* SPDX-License-Identifier: GPL-2.0 */

#define REQUEST(r_contents) \
	REQUEST_(REQUEST_NAME, REQUEST_NUM, REQUEST_IDX_KIND, I(r_contents))

#define __field(f_offset, f_bytes, f_name) \
	__field_(REQUEST_NAME, REQUEST_NUM, REQUEST_IDX_KIND, \
		 f_offset, f_bytes, f_name)

#define __array(f_offset, f_bytes, f_name) \
	__array_(REQUEST_NAME, REQUEST_NUM, REQUEST_IDX_KIND, \
		 f_offset, f_bytes, f_name)

#define __count(f_offset, f_bytes, f_name) \
	__count_(REQUEST_NAME, REQUEST_NUM, REQUEST_IDX_KIND, \
		 f_offset, f_bytes, f_name)
