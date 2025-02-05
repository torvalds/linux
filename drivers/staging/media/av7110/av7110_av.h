/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AV7110_AV_H_
#define _AV7110_AV_H_

struct av7110;

int av7110_set_vidmode(struct av7110 *av7110,
		       enum av7110_video_mode mode);

int av7110_record_cb(struct dvb_filter_pes2ts *p2t, u8 *buf, size_t len);
int av7110_pes_play(void *dest, struct dvb_ringbuffer *buf, int dlen);
int av7110_write_to_decoder(struct dvb_demux_feed *feed, const u8 *buf, size_t len);

int av7110_set_volume(struct av7110 *av7110, unsigned int volleft,
		      unsigned int volright);
int av7110_av_stop(struct av7110 *av7110, int av);
int av7110_av_start_record(struct av7110 *av7110, int av,
			   struct dvb_demux_feed *dvbdmxfeed);
int av7110_av_start_play(struct av7110 *av7110, int av);

void dvb_video_add_event(struct av7110 *av7110, struct video_event *event);

void av7110_p2t_init(struct av7110_p2t *p, struct dvb_demux_feed *feed);
void av7110_p2t_write(u8 const *buf, long length, u16 pid, struct av7110_p2t *p);

int av7110_av_register(struct av7110 *av7110);
void av7110_av_unregister(struct av7110 *av7110);
int av7110_av_init(struct av7110 *av7110);
void av7110_av_exit(struct av7110 *av7110);

#endif /* _AV7110_AV_H_ */
