// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Zorro Liu <zorro.liu@rock-chips.com>
 */

#ifndef _BUF_MANAGE_H_
#define _BUF_MANAGE_H_

#define BUF_ERROR	(-1)
#define BUF_SUCCESS	(0)

enum ebc_buf_status {
	buf_idle = 0,		//empty buf can be used
	buf_user = 1,		//buf get by user
	buf_dsp = 2,		//buf on dsp list
	buf_osd = 3,		//buf is osd buf
	buf_error = 4,
};

struct ebc_buf_s {
	enum ebc_buf_status status; //buffer status.
	unsigned long phy_addr; //buffer physical address.
	char *virt_addr; //buffer virtual address.
	char tid_name[TASK_COMM_LEN];
	int buf_mode;
	int len; //buffer length
	int win_x1;
	int win_y1;
	int win_x2;
	int win_y2;
};

struct ebc_buf_s *ebc_osd_buf_get(void);
struct ebc_buf_s *ebc_osd_buf_clone(void);
int ebc_buf_release(struct ebc_buf_s *release_buf);
int ebc_remove_from_dsp_buf_list(struct ebc_buf_s *remove_buf);
int ebc_add_to_dsp_buf_list(struct ebc_buf_s *dsp_buf);
int ebc_get_dsp_list_enum_num(void);
struct ebc_buf_s *ebc_dsp_buf_get(void);
struct ebc_buf_s *ebc_find_buf_by_phy_addr(unsigned long phy_addr);
struct ebc_buf_s *ebc_empty_buf_get(void);
unsigned long ebc_phy_buf_base_get(void);
char *ebc_virt_buf_base_get(void);
int ebc_buf_uninit(void);
int ebc_buf_init(unsigned long phy_start, char *mem_start, int men_len, int dest_buf_len, int max_buf_num);
#endif

