/*
 * CPUMF HW sampler functions and internal structures
 *
 *    Copyright IBM Corp. 2010
 *    Author(s): Heinz Graalfs <graalfs@de.ibm.com>
 */

#ifndef HWSAMPLER_H_
#define HWSAMPLER_H_

#include <linux/workqueue.h>

struct hws_qsi_info_block          /* QUERY SAMPLING information block  */
{ /* Bit(s) */
	unsigned int b0_13:14;      /* 0-13: zeros                       */
	unsigned int as:1;          /* 14: sampling authorisation control*/
	unsigned int b15_21:7;      /* 15-21: zeros                      */
	unsigned int es:1;          /* 22: sampling enable control       */
	unsigned int b23_29:7;      /* 23-29: zeros                      */
	unsigned int cs:1;          /* 30: sampling activation control   */
	unsigned int:1;             /* 31: reserved                      */
	unsigned int bsdes:16;      /* 4-5: size of sampling entry       */
	unsigned int:16;            /* 6-7: reserved                     */
	unsigned long min_sampl_rate; /* 8-15: minimum sampling interval */
	unsigned long max_sampl_rate; /* 16-23: maximum sampling interval*/
	unsigned long tear;         /* 24-31: TEAR contents              */
	unsigned long dear;         /* 32-39: DEAR contents              */
	unsigned int rsvrd0;        /* 40-43: reserved                   */
	unsigned int cpu_speed;     /* 44-47: CPU speed                  */
	unsigned long long rsvrd1;  /* 48-55: reserved                   */
	unsigned long long rsvrd2;  /* 56-63: reserved                   */
};

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

struct hws_data_entry {
	unsigned int def:16;        /* 0-15  Data Entry Format           */
	unsigned int R:4;           /* 16-19 reserved                    */
	unsigned int U:4;           /* 20-23 Number of unique instruct.  */
	unsigned int z:2;           /* zeros                             */
	unsigned int T:1;           /* 26 PSW DAT mode                   */
	unsigned int W:1;           /* 27 PSW wait state                 */
	unsigned int P:1;           /* 28 PSW Problem state              */
	unsigned int AS:2;          /* 29-30 PSW address-space control   */
	unsigned int I:1;           /* 31 entry valid or invalid         */
	unsigned int:16;
	unsigned int prim_asn:16;   /* primary ASN                       */
	unsigned long long ia;      /* Instruction Address               */
	unsigned long long gpp;     /* Guest Program Parameter		 */
	unsigned long long hpp;     /* Host Program Parameter		 */
};

struct hws_trailer_entry {
	unsigned int f:1;           /* 0 - Block Full Indicator          */
	unsigned int a:1;           /* 1 - Alert request control         */
	unsigned long:62;           /* 2 - 63: Reserved                  */
	unsigned long overflow;     /* 64 - sample Overflow count        */
	unsigned long timestamp;    /* 16 - time-stamp                   */
	unsigned long timestamp1;   /*                                   */
	unsigned long reserved1;    /* 32 -Reserved                      */
	unsigned long reserved2;    /*                                   */
	unsigned long progusage1;   /* 48 - reserved for programming use */
	unsigned long progusage2;   /*                                   */
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
