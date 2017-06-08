/*
 * Copyright (C) STMicroelectronics SA 2015
 * Author: Hugues Fruchet <hugues.fruchet@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef DELTA_IPC_H
#define DELTA_IPC_H

int delta_ipc_init(struct delta_dev *delta);
void delta_ipc_exit(struct delta_dev *delta);

/*
 * delta_ipc_open - open a decoding instance on firmware side
 * @ctx:		(in) delta context
 * @name:		(in) name of decoder to be used
 * @param:		(in) open command parameters specific to decoder
 *  @param.size:		(in) size of parameter
 *  @param.data:		(in) virtual address of parameter
 * @ipc_buf_size:	(in) size of IPC shared buffer between host
 *			     and copro used to share command data.
 *			     Client have to set here the size of the biggest
 *			     command parameters (+ status if any).
 *			     Allocation will be done in this function which
 *			     will give back to client in @ipc_buf the virtual
 *			     & physical addresses & size of shared IPC buffer.
 *			     All the further command data (parameters + status)
 *			     have to be written in this shared IPC buffer
 *			     virtual memory. This is done to avoid
 *			     unnecessary copies of command data.
 * @ipc_buf:		(out) allocated IPC shared buffer
 *  @ipc_buf.size:		(out) allocated size
 *  @ipc_buf.vaddr:		(out) virtual address where to copy
 *				      further command data
 * @hdl:		(out) handle of decoding instance.
 */

int delta_ipc_open(struct delta_ctx *ctx, const char *name,
		   struct delta_ipc_param *param, u32 ipc_buf_size,
		   struct delta_buf **ipc_buf, void **hdl);

/*
 * delta_ipc_set_stream - set information about stream to decoder
 * @hdl:		(in) handle of decoding instance.
 * @param:		(in) set stream command parameters specific to decoder
 *  @param.size:		(in) size of parameter
 *  @param.data:		(in) virtual address of parameter. Must be
 *				     within IPC shared buffer range
 */
int delta_ipc_set_stream(void *hdl, struct delta_ipc_param *param);

/*
 * delta_ipc_decode - frame decoding synchronous request, returns only
 *		      after decoding completion on firmware side.
 * @hdl:		(in) handle of decoding instance.
 * @param:		(in) decode command parameters specific to decoder
 *  @param.size:		(in) size of parameter
 *  @param.data:		(in) virtual address of parameter. Must be
 *				     within IPC shared buffer range
 * @status:		(in/out) decode command status specific to decoder
 *  @status.size:		(in) size of status
 *  @status.data:		(in/out) virtual address of status. Must be
 *					 within IPC shared buffer range.
 *					 Status is filled by decoding instance
 *					 after decoding completion.
 */
int delta_ipc_decode(void *hdl, struct delta_ipc_param *param,
		     struct delta_ipc_param *status);

/*
 * delta_ipc_close - close decoding instance
 * @hdl:		(in) handle of decoding instance to close.
 */
void delta_ipc_close(void *hdl);

#endif /* DELTA_IPC_H */
