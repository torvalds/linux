/**

 \file   as10x_cmd_stream.c

 \version $Id$

 \author: S. Martinelli

 ----------------------------------------------------------------------------\n
   (c) Copyright Abilis Systems SARL 2005-2009 All rigths reserved \n
   www.abilis.com                                                  \n
 ----------------------------------------------------------------------------\n

 \brief AS10x CMD, stream services

	AS10x CMD management: build command buffer, send command through
	selected port and wait for the response when required.

*/


#if defined(LINUX) && defined(__KERNEL__) /* linux kernel implementation */
#include <linux/kernel.h>
#include "as102_drv.h"
#elif defined(WIN32)
    #if defined(DDK) /* win32 ddk implementation */
	#include "wdm.h"
	#include "Device.h"
	#include "endian_mgmt.h" /* FIXME */
    #else /* win32 sdk implementation */
	#include <windows.h>
	#include "types.h"
	#include "util.h"
	#include "as10x_handle.h"
	#include "endian_mgmt.h"
    #endif
#else /* all other cases */
    #include <string.h>
    #include "types.h"
    #include "util.h"
    #include "as10x_handle.h"
    #include "endian_mgmt.h" /* FIXME */
#endif /* __KERNEL__ */

#include "as10x_cmd.h"


/**
   \brief  send add filter command to AS10x
   \param  phandle:   pointer to AS10x handle
   \param  filter:    TSFilter filter for DVB-T
   \param  pfilter_handle: pointer where to store filter handle
   \return 0 when no error, < 0 in case of error.
   \callgraph
*/
int as10x_cmd_add_PID_filter(as10x_handle_t* phandle,
			     struct as10x_ts_filter *filter) {
   int    error;
   struct as10x_cmd_t *pcmd, *prsp;

   ENTER();

   pcmd = phandle->cmd;
   prsp = phandle->rsp;

   /* prepare command */
   as10x_cmd_build(pcmd, (++phandle->cmd_xid),
		    sizeof(pcmd->body.add_pid_filter.req));

   /* fill command */
   pcmd->body.add_pid_filter.req.proc_id = cpu_to_le16(CONTROL_PROC_SETFILTER);
   pcmd->body.add_pid_filter.req.pid = cpu_to_le16(filter->pid);
   pcmd->body.add_pid_filter.req.stream_type = filter->type;

   if(filter->idx < 16)
	pcmd->body.add_pid_filter.req.idx = filter->idx;
   else
	pcmd->body.add_pid_filter.req.idx = 0xFF;

   /* send command */
   if(phandle->ops->xfer_cmd) {
      error = phandle->ops->xfer_cmd(phandle,
		       (uint8_t *) pcmd,
		       sizeof(pcmd->body.add_pid_filter.req) + HEADER_SIZE,
		       (uint8_t *) prsp,
		       sizeof(prsp->body.add_pid_filter.rsp) + HEADER_SIZE);
   }
   else{
      error = AS10X_CMD_ERROR;
   }

   if(error < 0) {
      goto out;
   }

   /* parse response */
   error = as10x_rsp_parse(prsp, CONTROL_PROC_SETFILTER_RSP);

   if(error == 0) {
     /* Response OK -> get response data */
     filter->idx = prsp->body.add_pid_filter.rsp.filter_id;
   }

out:
   LEAVE();
   return(error);
}

/**
   \brief  Send delete filter command to AS10x
   \param  phandle:       pointer to AS10x handle
   \param  filter_handle: filter handle
   \return 0 when no error, < 0 in case of error.
   \callgraph
*/
int as10x_cmd_del_PID_filter(as10x_handle_t* phandle,
			     uint16_t pid_value)
{

   int    error;
   struct as10x_cmd_t *pcmd, *prsp;

   ENTER();

   pcmd = phandle->cmd;
   prsp = phandle->rsp;

   /* prepare command */
   as10x_cmd_build(pcmd, (++phandle->cmd_xid),
		    sizeof(pcmd->body.del_pid_filter.req));

   /* fill command */
   pcmd->body.del_pid_filter.req.proc_id = cpu_to_le16(CONTROL_PROC_REMOVEFILTER);
   pcmd->body.del_pid_filter.req.pid = cpu_to_le16(pid_value);

   /* send command */
   if(phandle->ops->xfer_cmd){
      error = phandle->ops->xfer_cmd(phandle,
		       (uint8_t *) pcmd,
		       sizeof(pcmd->body.del_pid_filter.req) + HEADER_SIZE,
		       (uint8_t *) prsp,
		       sizeof(prsp->body.del_pid_filter.rsp) + HEADER_SIZE);
   }
   else{
      error = AS10X_CMD_ERROR;
   }

   if(error < 0) {
      goto out;
   }

   /* parse response */
   error = as10x_rsp_parse(prsp, CONTROL_PROC_REMOVEFILTER_RSP);

out:
   LEAVE();
   return(error);
}

/**
   \brief Send start streaming command to AS10x
   \param  phandle:   pointer to AS10x handle
   \return 0 when no error, < 0 in case of error. 
   \callgraph
*/
int as10x_cmd_start_streaming(as10x_handle_t* phandle)
{
   int error;
   struct as10x_cmd_t *pcmd, *prsp;

   ENTER();

   pcmd = phandle->cmd;
   prsp = phandle->rsp;

   /* prepare command */
   as10x_cmd_build(pcmd, (++phandle->cmd_xid),
		    sizeof(pcmd->body.start_streaming.req));

   /* fill command */
   pcmd->body.start_streaming.req.proc_id =
				   cpu_to_le16(CONTROL_PROC_START_STREAMING);

   /* send command */
   if(phandle->ops->xfer_cmd){
      error = phandle->ops->xfer_cmd(phandle,
		       (uint8_t *) pcmd,
		       sizeof(pcmd->body.start_streaming.req) + HEADER_SIZE,
		       (uint8_t *) prsp,
		       sizeof(prsp->body.start_streaming.rsp) + HEADER_SIZE);
   }
   else{
      error = AS10X_CMD_ERROR;
   }

   if(error < 0) {
      goto out;
   }

   /* parse response */
   error = as10x_rsp_parse(prsp, CONTROL_PROC_START_STREAMING_RSP);

out:
   LEAVE();
   return(error);
}

/**
   \brief Send stop streaming command to AS10x
   \param  phandle:   pointer to AS10x handle
   \return 0 when no error, < 0 in case of error. 
   \callgraph
*/
int as10x_cmd_stop_streaming(as10x_handle_t* phandle)
{
   int8_t error;
   struct as10x_cmd_t *pcmd, *prsp;

   ENTER();

   pcmd = phandle->cmd;
   prsp = phandle->rsp;

   /* prepare command */
   as10x_cmd_build(pcmd, (++phandle->cmd_xid),
		    sizeof(pcmd->body.stop_streaming.req));

   /* fill command */
   pcmd->body.stop_streaming.req.proc_id =
				   cpu_to_le16(CONTROL_PROC_STOP_STREAMING);

   /* send command */
   if(phandle->ops->xfer_cmd){
      error = phandle->ops->xfer_cmd(phandle,
		       (uint8_t *) pcmd,
		       sizeof(pcmd->body.stop_streaming.req) + HEADER_SIZE,
		       (uint8_t *) prsp,
		       sizeof(prsp->body.stop_streaming.rsp) + HEADER_SIZE);
   }
   else{
      error = AS10X_CMD_ERROR;
   }

   if(error < 0) {
      goto out;
   }

   /* parse response */
   error = as10x_rsp_parse(prsp, CONTROL_PROC_STOP_STREAMING_RSP);

out:
   LEAVE();
   return(error);
}


