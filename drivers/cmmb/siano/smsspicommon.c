/****************************************************************

Siano Mobile Silicon, Inc.
MDTV receiver kernel modules.
Copyright (C) 2006-2008, Uri Shkolnik

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

 This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

****************************************************************/
#include "smsspicommon.h"
#include "smsdbg_prn.h"
#include "smscoreapi.h"

extern volatile bool g_libdownload;


static struct _rx_buffer_st *smsspi_handle_unused_bytes_buf(
		struct _spi_dev *dev,
		struct _rx_buffer_st *buf, int offset, int len,
		int unused_bytes)
{
	struct _rx_buffer_st *tmp_buf;


	tmp_buf = dev->cb.allocate_rx_buf(dev->context,
		RX_PACKET_SIZE);


	if (!tmp_buf) {
		sms_err("Failed to allocate RX buffer.\n");
		return NULL;
	}
	if (unused_bytes > 0) {
		/* Copy the remaining bytes to the end of
		alignment block (256 bytes) so next read
		will be aligned. */
		int align_block =
			(((unused_bytes + SPI_PACKET_SIZE -
			1) >> SPI_PACKET_SIZE_BITS) <<
			SPI_PACKET_SIZE_BITS);
		memset(tmp_buf->ptr, 0,
			align_block - unused_bytes);
		memcpy((char *)tmp_buf->ptr +
			(align_block - unused_bytes),
			(char *)buf->ptr + offset + len -
			unused_bytes, unused_bytes);
	}
	//sms_info("smsspi_handle_unused_bytes_buf unused_bytes=0x%x offset=0x%x len=0x%x \n",unused_bytes,offset,len);
	return tmp_buf;
}

static struct _rx_buffer_st *smsspi_common_find_msg(struct _spi_dev *dev,
		struct _rx_buffer_st *buf, int offset, int len,
		int *unused_bytes, int *missing_bytes)
{
	int i;
	int recieved_bytes, padded_msg_len;
	int align_fix;
	int msg_offset;
	unsigned char *ptr = (unsigned char *)buf->ptr + offset;
	if (unused_bytes == NULL || missing_bytes == NULL)
		return NULL;

	*missing_bytes = 0;
	*unused_bytes = 0;

	//sms_info("entering with %d bytes.\n", len);
	for (i = 0; i < len; i++, ptr++) {
		switch (dev->rxState) {
		case RxsWait_a5:
			dev->rxState =
			    ((*ptr & 0xff) == 0xa5) ? RxsWait_5a : RxsWait_a5;
			dev->rxPacket.msg_offset =
			    (unsigned long)ptr - (unsigned long)buf->ptr + 4;
			break;
		case RxsWait_5a:
			if ((*ptr & 0xff) == 0x5a) {
				dev->rxState = RxsWait_e7;
			}
			else {
				dev->rxState = RxsWait_a5;
				i--;
				ptr--;	// re-scan current byte
			}
			//sms_info("state %d.\n", dev->rxState);
			break;
		case RxsWait_e7:
			if ((*ptr & 0xff) == 0xe7) {
				dev->rxState = RxsWait_7e;
			}
			else {
				dev->rxState = RxsWait_a5;
				i--;
				ptr--;	// re-scan current byte
			}
			//sms_info("state %d.\n", dev->rxState);
			break;
		case RxsWait_7e:
			if ((*ptr & 0xff) == 0x7e) {
				dev->rxState = RxsTypeH;
			}
			else {
				dev->rxState = RxsWait_a5;
				i--;
				ptr--;	// re-scan current byte
			}
			//sms_info("state %d.\n", dev->rxState);
			break;
		case RxsTypeH:
			dev->rxPacket.msg_buf = buf;
			dev->rxPacket.msg_offset =
			    (unsigned long)ptr - (unsigned long)buf->ptr;
			dev->rxState = RxsTypeL;
			//sms_info("state %d.\n", dev->rxState);
			break;
		case RxsTypeL:
			dev->rxState = RxsGetSrcId;
			//sms_info("state %d.\n", dev->rxState);
			break;
		case RxsGetSrcId:
			dev->rxState = RxsGetDstId;
			//sms_info("state %d.\n", dev->rxState);
			break;
		case RxsGetDstId:
			dev->rxState = RxsGetLenL;
			//sms_info("state %d.\n", dev->rxState);
			break;
		case RxsGetLenL:
			dev->rxState = RxsGetLenH;
			dev->rxPacket.msg_len = (*ptr & 0xff);
			//sms_info("state %d.\n", dev->rxState);
			break;
		case RxsGetLenH:
			dev->rxState = RxsFlagsL;
			dev->rxPacket.msg_len += (*ptr & 0xff) << 8;
			//sms_info("state %d.\n", dev->rxState);
			break;
		case RxsFlagsL:
			dev->rxState = RxsFlagsH;
			dev->rxPacket.msg_flags = (*ptr & 0xff);
			//sms_info("state %d.\n", dev->rxState);
			break;
		case RxsFlagsH:
			dev->rxState = RxsData;
			dev->rxPacket.msg_flags += (*ptr & 0xff) << 8;
			//sms_info("state %d.\n", dev->rxState);
			break;
		case RxsData:
			recieved_bytes =
			    len + offset - dev->rxPacket.msg_offset;
			padded_msg_len =
			    ((dev->rxPacket.msg_len + 4 + SPI_PACKET_SIZE -
			      1) >> SPI_PACKET_SIZE_BITS) <<
			    SPI_PACKET_SIZE_BITS;
			if (recieved_bytes < padded_msg_len) {
				*unused_bytes = 0;
				*missing_bytes = padded_msg_len -
						recieved_bytes;


				return buf;
			}
			dev->rxState = RxsWait_a5;
			if (dev->cb.msg_found_cb) {
				align_fix = 0;
				if (dev->rxPacket.
				    msg_flags & MSG_HDR_FLAG_SPLIT_MSG_HDR) {
					align_fix =
					    (dev->rxPacket.
					     msg_flags >> 8) & 0x3;
					/* The FW aligned the message data
					therefore - alignment bytes should be
					thrown away. Throw the alignment bytes
					by moving the header ahead over the
					alignment bytes. */
					if (align_fix) {
						int length;
						ptr =
						    (unsigned char *)dev->rxPacket.
						    msg_buf->ptr +
						    dev->rxPacket.msg_offset;

						/* Restore header to original
						state before alignment changes
						*/
						length =
						    (ptr[5] << 8) | ptr[4];
						length -= align_fix;
						ptr[5] = length >> 8;
						ptr[4] = length & 0xff;
						/* Zero alignment flags */
						ptr[7] &= 0xfc;

						for (i = MSG_HDR_LEN - 1;
						     i >= 0; i--) {
							ptr[i + align_fix] =
							    ptr[i];
						}
						dev->rxPacket.msg_offset +=
						    align_fix;
					}
				}

				sms_info("Msg found and sent to callback func.\n");

				/* force all messages to start on
				 * 4-byte boundary */
				msg_offset = dev->rxPacket.msg_offset;
				if (msg_offset & 0x3) {
					msg_offset &= (~0x3);
					memmove((unsigned char *)
						(dev->rxPacket.msg_buf->ptr)
						+ msg_offset,
						(unsigned char *)
						(dev->rxPacket.msg_buf->ptr)
						+ dev->rxPacket.msg_offset,
						dev->rxPacket.msg_len -
						align_fix);
				}

				*unused_bytes =
				    len + offset - dev->rxPacket.msg_offset -
				    dev->rxPacket.msg_len;

				/* In any case we got here - unused_bytes
				 * should not be 0 Because we want to force
				 * reading at least 256 after the end
				 of any found message */
				if (*unused_bytes == 0)
					*unused_bytes = -1;

				buf = smsspi_handle_unused_bytes_buf(dev, buf,
						offset, len, *unused_bytes);



				dev->cb.msg_found_cb(dev->context,
							 dev->rxPacket.msg_buf,
							 msg_offset,
							 dev->rxPacket.msg_len -
							 align_fix);


				*missing_bytes = 0;
				return buf;
			} else {
				sms_info("Msg found but no callback. therefore - thrown away.\n");
			}
			sms_info("state %d.\n", dev->rxState);
			break;
		}
	}

	if (dev->rxState == RxsWait_a5) {
		*unused_bytes = 0;
		*missing_bytes = 0;

		return buf;
	} else {
		/* Workaround to corner case: if the last byte of the buffer
		is "a5" (first byte of the preamble), the host thinks it should
		send another 256 bytes.  In case the a5 is the firmware
		underflow byte, this will cause an infinite loop, so we check
		for this case explicity. */
		if (dev->rxState == RxsWait_5a) {
			if ((*(ptr - 2) == 0xa5) || (*((unsigned int*)(ptr-4)) == *((unsigned int*)(ptr-8)))) {
				dev->rxState = RxsWait_a5;
				*unused_bytes = 0;
				*missing_bytes = 0;

				return buf;
			}
		}

		if ((dev->rxState == RxsWait_5a) && (*(ptr - 2) == 0xa5)) {
			dev->rxState = RxsWait_a5;
			*unused_bytes = 0;
			*missing_bytes = 0;

			return buf;
		}

		if (dev->rxPacket.msg_offset >= (SPI_PACKET_SIZE + 4))
			/* adding 4 for the preamble. */
		{		/*The packet will be copied to a new buffer
				   and rescaned by the state machine */
			struct _rx_buffer_st *tmp_buf = buf;
			*unused_bytes = dev->rxState - RxsWait_a5;
			tmp_buf = smsspi_handle_unused_bytes_buf(dev, buf,
					offset, len, *unused_bytes);
			dev->rxState = RxsWait_a5;

			dev->cb.free_rx_buf(dev->context, buf);


			*missing_bytes = 0;
			return tmp_buf;
		} else {
			/* report missing bytes and continue
			   with message scan. */
			*unused_bytes = 0;
			*missing_bytes = SPI_PACKET_SIZE;
			return buf;
		}
	}
}

void smsspi_common_transfer_msg(struct _spi_dev *dev, struct _spi_msg *txmsg,
				int padding_allowed)
{
	int len, bytes_to_transfer;
	unsigned long tx_phy_addr;
	int missing_bytes, tx_bytes;
	int offset, unused_bytes;
	int align_block;
	char *txbuf;
	struct _rx_buffer_st *buf, *tmp_buf;

#if 	SIANO_HALFDUPLEX
	if (txmsg){
		tx_bytes = txmsg->len;
		if (padding_allowed)
			bytes_to_transfer =
			    (((tx_bytes + SPI_PACKET_SIZE -
			       1) >> SPI_PACKET_SIZE_BITS) <<
			     SPI_PACKET_SIZE_BITS);
		else
			bytes_to_transfer = (((tx_bytes + 3) >> 2) << 2);
		txbuf = txmsg->buf;
		tx_phy_addr = txmsg->buf_phy_addr;
		len = min(bytes_to_transfer, RX_PACKET_SIZE);
		dev->cb.transfer_data_cb(dev->phy_context,(unsigned char *)txbuf,tx_phy_addr,NULL,NULL,len);
	} else
#endif

	{
	
//	sms_info("g_libdownload == %d!!!!!!!!!!!!!!!!!\n",g_libdownload);
	if(g_libdownload == false)
		{
//		sms_info("g_libdownload == false!!!!!!!!!!!!!!!!!\n");
	len = 0;
	if (!dev->cb.transfer_data_cb) {
		sms_err("function called while module is not initialized.\n");
		return;
	}
	if (txmsg == 0) {
		bytes_to_transfer = SPI_PACKET_SIZE;
		txbuf = 0;
		tx_phy_addr = 0;
		tx_bytes = 0;
	} else {
		tx_bytes = txmsg->len;
		if (padding_allowed)
			bytes_to_transfer =
			    (((tx_bytes + SPI_PACKET_SIZE -
			       1) >> SPI_PACKET_SIZE_BITS) <<
			     SPI_PACKET_SIZE_BITS);
		else
			bytes_to_transfer = (((tx_bytes + 3) >> 2) << 2);
		txbuf = txmsg->buf;
		tx_phy_addr = txmsg->buf_phy_addr;
	}
	offset = 0;
	unused_bytes = 0;

	buf =
	    dev->cb.allocate_rx_buf(dev->context,
				    RX_PACKET_SIZE + SPI_PACKET_SIZE);


	if (!buf) {
		sms_err("Failed to allocate RX buffer.\n");
		return;
	}
	while (bytes_to_transfer || unused_bytes) {
		if ((unused_bytes <= 0) && (bytes_to_transfer > 0)) {
			len = min(bytes_to_transfer, RX_PACKET_SIZE);
			//sms_info("transfering block of %d bytes\n", len);
			dev->cb.transfer_data_cb(dev->phy_context,
					(unsigned char *)txbuf,
					tx_phy_addr,
					(unsigned char *)buf->ptr + offset,
					buf->phy_addr + offset, len);
		}

		tmp_buf =
		    smsspi_common_find_msg(dev, buf, offset, len,
					   &unused_bytes, &missing_bytes);


		//sms_info("smsspi_common_transfer_msg unused_bytes=0x%x missing_bytes=0x%x\n", unused_bytes, missing_bytes);

		if (bytes_to_transfer)
			bytes_to_transfer -= len;

		if (tx_bytes)
			tx_bytes -= len;

		if (missing_bytes)
			offset += len;

		if (unused_bytes) {
			/* In this case tmp_buf is a new buffer allocated
			 * in smsspi_common_find_msg
			 * and it already contains the unused bytes */
			if (unused_bytes > 0) {
				align_block =
				    (((unused_bytes + SPI_PACKET_SIZE -
				       1) >> SPI_PACKET_SIZE_BITS) <<
				     SPI_PACKET_SIZE_BITS);
				len = align_block;
			}
			offset = 0;
			buf = tmp_buf;

		}
		if (tx_bytes <= 0) {
			txbuf = 0;
			tx_bytes = 0;
		}
		if (bytes_to_transfer < missing_bytes) {
			bytes_to_transfer =
			    (((missing_bytes + SPI_PACKET_SIZE -
			       1) >> SPI_PACKET_SIZE_BITS) <<
			     SPI_PACKET_SIZE_BITS);
			sms_info("a message was found, adding bytes to transfer, txmsg %d, total %d\n"
			, tx_bytes, bytes_to_transfer);
		}
	}


	dev->cb.free_rx_buf(dev->context, buf);
}
}

}

int smsspicommon_init(struct _spi_dev *dev, void *context, void *phy_context,
		      struct _spi_dev_cb_st *cb)
{
	sms_info("entering.\n");
	if (cb->transfer_data_cb == 0 ||
	    cb->msg_found_cb == 0 ||
	    cb->allocate_rx_buf == 0 || cb->free_rx_buf == 0) {
		sms_err("Invalid input parameters of init routine.\n");
		return -1;
	}
	dev->context = context;
	dev->phy_context = phy_context;
	memcpy(&dev->cb, cb, sizeof(struct _spi_dev_cb_st));
	dev->rxState = RxsWait_a5;
	sms_info("exiting.\n");
	return 0;
}
