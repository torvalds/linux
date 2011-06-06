#ifndef __RECV_OSDEP_H_
#define __RECV_OSDEP_H_

#include "osdep_service.h"
#include "drv_types.h"
#include <linux/skbuff.h>

sint _r8712_init_recv_priv(struct recv_priv *precvpriv,
			   struct _adapter *padapter);
void _r8712_free_recv_priv(struct recv_priv *precvpriv);
s32  r8712_recv_entry(union recv_frame *precv_frame);
void r8712_recv_indicatepkt(struct _adapter *adapter,
			    union recv_frame *precv_frame);
void r8712_handle_tkip_mic_err(struct _adapter *padapter, u8 bgroup);
int r8712_init_recv_priv(struct recv_priv *precvpriv,
			 struct _adapter *padapter);
void r8712_free_recv_priv(struct recv_priv *precvpriv);
int r8712_os_recv_resource_alloc(struct _adapter *padapter,
				 union recv_frame *precvframe);
int r8712_os_recvbuf_resource_alloc(struct _adapter *padapter,
				    struct recv_buf *precvbuf);
int r8712_os_recvbuf_resource_free(struct _adapter *padapter,
				   struct recv_buf *precvbuf);
void r8712_os_read_port(struct _adapter *padapter, struct recv_buf *precvbuf);
void r8712_init_recv_timer(struct recv_reorder_ctrl *preorder_ctrl);

#endif
