/*******************************************************************
 *
 *  Copyright C 2012 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *
 *  Author: Amlogic Software
 *  Created: 2012/3/13   19:46
 *
 *******************************************************************/

#define AM_CSI2_BUFF_STATUS_NULL         0x00
#define AM_CSI2_BUFF_STATUS_FREE         0x01  //can be used by mipi receiver
#define AM_CSI2_BUFF_STATUS_AVAIL       0x02  // can be used to fill buffer
#define AM_CSI2_BUFF_STATUS_BUSY        0x03  // pop to use

typedef struct bufq_s{
    am_csi2_frame_t *pool[CSI2_BUF_POOL_SIZE];
    u32 rd_index;
    u32 wr_index;
    u32 count;
} bufq_t;

typedef struct mipi_buf_s{
    bufq_t available_q;
    bufq_t free_q;
    spinlock_t q_lock;
}mipi_buf_t;

bool bufq_empty(bufq_t *q);
bool bufq_full(bufq_t *q);
void bufq_push(bufq_t *q, am_csi2_frame_t *frame);
am_csi2_frame_t *bufq_pop(bufq_t *q);

bool bufq_empty_free(mipi_buf_t* buff);
bool bufq_empty_available(mipi_buf_t* buff);

void bufq_push_free(mipi_buf_t* buff, am_csi2_frame_t *frame);
void bufq_push_available(mipi_buf_t* buff, am_csi2_frame_t *frame);

am_csi2_frame_t *bufq_pop_free(mipi_buf_t* buff);
am_csi2_frame_t *bufq_pop_available(mipi_buf_t* buff);

void bufq_init(mipi_buf_t* buff, am_csi2_frame_t* frame, unsigned count);

