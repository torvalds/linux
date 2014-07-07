/*
 * CPUMF HW sampler functions and internal structures
 *
 *    Copyright IBM Corp. 2010
 *    Author(s): Heinz Graalfs <graalfs@de.ibm.com>
 */

#ifndef HWSAMPLER_H_
#define HWSAMPLER_H_

#include <linux/workqueue.h>
#include <asm/cpu_mf.h>

struct hws_ssctl_request_block     /* SET SAMPLING CONTROLS req block   */
{ /* bytes 0 - 7  Bit(s) */
	unsigned int s:1;           /* 0: maximum buffer indicator       */
	unsigned int h:1;           /* 1: part. level reserved for VM use*/
	unsigned long b2_53:52;     /* 2-53: zeros                       */
	unsigned int es:1;          /* 54: sampling enable control       */
	unsigned int b55_61:7;      /* 55-61: - zeros                    */
	unsigned int cs:1;          /* 62: sampling activation control   */
	unsigned int b63:1;         /* 63: zero                          */
	unsigned long interval;     /* 8-15: sampling interval           */
	unsigned long tear;         /* 16-23: TEAR contents              */
	unsigned long dear;         /* 24-31: DEAR contents              */
	/* 32-63:                                                        */
	unsigned long rsvrd1;       /* reserved                          */
	unsigned long rsvrd2;       /* reserved                          */
	unsigned long rsvrd3;       /* reserved                          */
	unsigned long rsvrd4;       /* reserved                          */
};

struct hws_cpu_buffer {
	unsigned long first_sdbt;       /* @ of 1st SDB-Table for this CP*/
	unsigned long worker_entry;
	unsigned long sample_overflow;  /* taken from SDB ...            */
	struct hws_qsi_info_block qsi;
	struct hws_ssctl_request_block ssctl;
	struct work_struct worker;
	atomic_t ext_params;
	unsigned long req_alert;
	unsigned long loss_of_sample_data;
	unsigned long invalid_entry_address;
	unsigned long incorrect_sdbt_entry;
	unsigned long sample_auth_change_alert;
	unsigned int finish:1;
	unsigned int oom:1;
	unsigned int stop_mode:1;
};

int hwsampler_setup(void);
int hwsampler_shutdown(void);
int hwsampler_allocate(unsigned long sdbt, unsigned long sdb);
int hwsampler_deallocate(void);
unsigned long hwsampler_query_min_interval(void);
unsigned long hwsampler_query_max_interval(void);
int hwsampler_start_all(unsigned long interval);
int hwsampler_stop_all(void);
int hwsampler_deactivate(unsigned int cpu);
int hwsampler_activate(unsigned int cpu);
unsigned long hwsampler_get_sample_overflow_count(unsigned int cpu);

#endif /*HWSAMPLER_H_*/
