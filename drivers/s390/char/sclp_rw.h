/*
 * interface to the SCLP-read/write driver
 *
 * Copyright IBM Corporation 1999, 2009
 *
 * Author(s): Martin Peschke <mpeschke@de.ibm.com>
 *	      Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __SCLP_RW_H__
#define __SCLP_RW_H__

#include <linux/list.h>

struct mto {
	u16 length;
	u16 type;
	u16 line_type_flags;
	u8  alarm_control;
	u8  _reserved[3];
} __attribute__((packed));

struct go {
	u16 length;
	u16 type;
	u32 domid;
	u8  hhmmss_time[8];
	u8  th_time[3];
	u8  reserved_0;
	u8  dddyyyy_date[7];
	u8  _reserved_1;
	u16 general_msg_flags;
	u8  _reserved_2[10];
	u8  originating_system_name[8];
	u8  job_guest_name[8];
} __attribute__((packed));

struct mdb_header {
	u16 length;
	u16 type;
	u32 tag;
	u32 revision_code;
} __attribute__((packed));

struct mdb {
	struct mdb_header header;
	struct go go;
} __attribute__((packed));

struct msg_buf {
	struct evbuf_header header;
	struct mdb mdb;
} __attribute__((packed));

struct write_sccb {
	struct sccb_header header;
	struct msg_buf msg_buf;
} __attribute__((packed));

/* The number of empty mto buffers that can be contained in a single sccb. */
#define NR_EMPTY_MTO_PER_SCCB ((PAGE_SIZE - sizeof(struct sclp_buffer) - \
			sizeof(struct write_sccb)) / sizeof(struct mto))

/*
 * data structure for information about list of SCCBs (only for writing),
 * will be located at the end of a SCCBs page
 */
struct sclp_buffer {
	struct list_head list;		/* list_head for sccb_info chain */
	struct sclp_req request;
	struct write_sccb *sccb;
	char *current_line;
	int current_length;
	int retry_count;
	/* output format settings */
	unsigned short columns;
	unsigned short htab;
	/* statistics about this buffer */
	unsigned int mto_char_sum;	/* # chars in sccb */
	unsigned int mto_number;	/* # mtos in sccb */
	/* Callback that is called after reaching final status. */
	void (*callback)(struct sclp_buffer *, int);
};

int sclp_rw_init(void);
struct sclp_buffer *sclp_make_buffer(void *, unsigned short, unsigned short);
void *sclp_unmake_buffer(struct sclp_buffer *);
int sclp_buffer_space(struct sclp_buffer *);
int sclp_write(struct sclp_buffer *buffer, const unsigned char *, int);
int sclp_emit_buffer(struct sclp_buffer *,void (*)(struct sclp_buffer *,int));
void sclp_set_columns(struct sclp_buffer *, unsigned short);
void sclp_set_htab(struct sclp_buffer *, unsigned short);
int sclp_chars_in_buffer(struct sclp_buffer *);

#ifdef CONFIG_SCLP_CONSOLE
void sclp_console_pm_event(enum sclp_pm_event sclp_pm_event);
#else
static inline void sclp_console_pm_event(enum sclp_pm_event sclp_pm_event) { }
#endif

#endif	/* __SCLP_RW_H__ */
