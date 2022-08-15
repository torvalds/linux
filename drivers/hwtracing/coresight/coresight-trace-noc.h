/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define TRACE_NOC_CTRL	0x008
#define TRACE_NOC_XLD	0x010
#define TRACE_NOC_FREQVAL	0x018
#define TRACE_NOC_SYNCR	0x020

/* Enable generation of output ATB traffic.*/
#define TRACE_NOC_CTRL_PORTEN	BIT(0)
/* Writing 1 to initiate a flush sequence.*/
#define TRACE_NOC_CTRL_FLUSHREQ	BIT(1)
/* 0: sequence in progress; 1: sequence has been completed.*/
#define TRACE_NOC_CTRL_FLUSHSTATUS	BIT(2)
/* Writing 1 to issue a FREQ or FREQ_TS packet*/
#define TRACE_NOC_CTRL_FREQTSREQ	BIT(5)
/* Sets the type of issued ATB FLAG packets. 0: 'FLAG' packets; 1: 'FLAG_TS' packets.*/
#define TRACE_NOC_CTRL_FLAGTYPE	BIT(7)
/* sets the type of issued ATB FREQ packets. 0: 'FREQ' packets; 1: 'FREQ_TS' packets.*/
#define TRACE_NOC_CTRL_FREQTYPE	BIT(8)

DEFINE_CORESIGHT_DEVLIST(trace_noc_devs, "traceNoc");

/**
 * struct trace_noc_drvdata - specifics associated to a trace noc component
 * @base:	memory mapped base address for this component.
 * @dev:	device node for trace_noc_drvdata.
 * @csdev:	component vitals needed by the framework.
 * @spinlock:	lock for the drvdata.
 * @enable:	status of the component.
 * @flushReq:	Issue a flush request or not.
 * @freqTsReq:	Issue a freq_ts request or not.
 * @atid:	id for the trace packet.
 * @freq_req_val:	 set frequency values carried by 'FREQ' and 'FREQ_TS' packets.
 * @flushStatus:	0: sequence in progress; 1: sequence has been completed.
 * @freqType:	0: 'FREQ' packets; 1: 'FREQ_TS' packets.
 * @flagType:	0: 'FLAG' packets; 1: 'FLAG_TS' packets.
 */
struct trace_noc_drvdata {
	void __iomem		*base;
	struct device		*dev;
	struct coresight_device	*csdev;
	spinlock_t		spinlock;
	bool			enable;
	bool			flushReq;
	bool			freqTsReq;
	u32			atid;
	u32			freq_req_val;
	u32			flushStatus;
	u32			freqType;
	u32			flagType;
};

/* freq type */
enum freq_type {
	FREQ,
	FREQ_TS,
};

/* flag type */
enum flag_type {
	FLAG,
	FLAG_TS,
};
