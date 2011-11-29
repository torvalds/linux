/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010, Atheros Communications, Inc.
 */

#include "sigma_dut.h"

typedef unsigned int u32;
typedef unsigned char u8;

#define WPA_GET_BE32(a) ((((u8) (a)[0]) << 24) | (((u8) (a)[1]) << 16) | \
			 (((u8) (a)[2]) << 8) | ((u8) (a)[3]))
#define WPA_PUT_BE32(a, val)					\
	do {							\
		(a)[0] = (u8) ((((u32) (val)) >> 24) & 0xff);	\
		(a)[1] = (u8) ((((u32) (val)) >> 16) & 0xff);	\
		(a)[2] = (u8) ((((u32) (val)) >> 8) & 0xff);	\
		(a)[3] = (u8) (((u32) (val)) & 0xff);		\
	} while (0)


static int cmd_traffic_agent_config(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	struct sigma_stream *s;
	const char *val;
	char buf[100];

	if (dut->num_streams == MAX_SIGMA_STREAMS) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,No more "
			  "concurrent traffic streams supported");
		return 0;
	}

	s = &dut->streams[dut->num_streams];
	memset(s, 0, sizeof(*s));
	s->sock = -1;

	val = get_param(cmd, "profile");
	if (!val)
		return -1;

	if (strcasecmp(val, "File_Transfer") == 0)
		s->profile = SIGMA_PROFILE_FILE_TRANSFER;
	else if (strcasecmp(val, "Multicast") == 0)
		s->profile = SIGMA_PROFILE_MULTICAST;
	else if (strcasecmp(val, "IPTV") == 0)
		s->profile = SIGMA_PROFILE_IPTV;
	else if (strcasecmp(val, "Transaction") == 0)
		s->profile = SIGMA_PROFILE_TRANSACTION;
	else if (strcasecmp(val, "Start_Sync") == 0)
		s->profile = SIGMA_PROFILE_START_SYNC;
	else {
		send_resp(dut, conn, SIGMA_INVALID, "errorCode,Unsupported "
			  "profile");
		return 0;
	}

	val = get_param(cmd, "direction");
	if (!val)
		return -1;
	if (strcasecmp(val, "send") == 0)
		s->sender = 1;
	else if (strcasecmp(val, "receive") == 0)
		s->sender = 0;
	else
		return -1;

	val = get_param(cmd, "destination");
	if (val) {
		if (inet_aton(val, &s->dst) == 0)
			return -1;
	}

	val = get_param(cmd, "source");
	if (val) {
		if (inet_aton(val, &s->src) == 0)
			return -1;
	}

	val = get_param(cmd, "destinationPort");
	if (val)
		s->dst_port = atoi(val);

	val = get_param(cmd, "sourcePort");
	if (val)
		s->src_port = atoi(val);

	val = get_param(cmd, "frameRate");
	if (val)
		s->frame_rate = atoi(val);

	val = get_param(cmd, "duration");
	if (val)
		s->duration = atoi(val);

	val = get_param(cmd, "payloadSize");
	if (val)
		s->payload_size = atoi(val);

	val = get_param(cmd, "startDelay");
	if (val)
		s->start_delay = atoi(val);

	val = get_param(cmd, "maxCnt");
	if (val)
		s->max_cnt = atoi(val);

	val = get_param(cmd, "trafficClass");
	if (val) {
		if (strcasecmp(val, "Voice") == 0)
			s->tc = SIGMA_TC_VOICE;
		else if (strcasecmp(val, "Video") == 0)
			s->tc = SIGMA_TC_VIDEO;
		else if (strcasecmp(val, "Background") == 0)
			s->tc = SIGMA_TC_BACKGROUND;
		else if (strcasecmp(val, "BestEffort") == 0)
			s->tc = SIGMA_TC_BEST_EFFORT;
		else
			return -1;
	}

	dut->num_streams++;

	snprintf(buf, sizeof(buf), "streamID,%d", dut->num_streams);
	send_resp(dut, conn, SIGMA_COMPLETE, buf);
	return 0;
}


static void stop_stream(struct sigma_stream *s)
{
	if (s->started) {
		pthread_join(s->thr, NULL);
		close(s->sock);
		s->sock = -1;
	}
}


static int cmd_traffic_agent_reset(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd)
{
	int i;
	for (i = 0; i < dut->num_streams; i++) {
		struct sigma_stream *s = &dut->streams[i];
		s->stop = 1;
		stop_stream(s);
	}
	dut->num_streams = 0;
	memset(&dut->streams, 0, sizeof(dut->streams));
	return 1;
}


static int get_stream_id(const char *str, int streams[MAX_SIGMA_STREAMS])
{
	int count;

	count = 0;
	for (;;) {
		if (count == MAX_SIGMA_STREAMS)
			return -1;
		streams[count] = atoi(str);
		if (streams[count] == 0)
			return -1;
		count++;
		str = strchr(str, ' ');
		if (str == NULL)
			break;
		while (*str == ' ')
			str++;
	}

	return count;
}


static int open_socket_file_transfer(struct sigma_stream *s)
{
	struct sockaddr_in addr;

	s->sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s->sock < 0) {
		perror("socket");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(s->sender ? s->src_port : s->dst_port);
	if (bind(s->sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("bind");
		close(s->sock);
		s->sock = -1;
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = s->sender ? s->dst.s_addr : s->src.s_addr;
	addr.sin_port = htons(s->sender ? s->dst_port : s->src_port);
	if (connect(s->sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("connect");
		close(s->sock);
		s->sock = -1;
		return -1;
	}

	return 0;
}


static int set_socket_prio(struct sigma_stream *s)
{
	int tos = 0x00;

	switch (s->tc) {
	case SIGMA_TC_VOICE:
		tos = 0xd0;
		break;
	case SIGMA_TC_VIDEO:
		tos = 0xa0;
		break;
	case SIGMA_TC_BACKGROUND:
		tos = 0x20;
		break;
	case SIGMA_TC_BEST_EFFORT:
		tos = 0x00;
		break;
	}

	if (setsockopt(s->sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) < 0) {
		perror("setsockopt");
		return -1;
	}

	return 0;
}


static int open_socket(struct sigma_dut *dut, struct sigma_stream *s)
{
	switch (s->profile) {
	case SIGMA_PROFILE_FILE_TRANSFER:
		return open_socket_file_transfer(s);
	case SIGMA_PROFILE_MULTICAST:
		sigma_dut_print( DUT_MSG_INFO, "Traffic stream profile %d "
				"not yet supported", s->profile);
		/* TODO */
		break;
	case SIGMA_PROFILE_IPTV:
		if (open_socket_file_transfer(s) < 0)
			return -1;
		return set_socket_prio(s);
	case SIGMA_PROFILE_TRANSACTION:
		return open_socket_file_transfer(s);
	case SIGMA_PROFILE_START_SYNC:
		sigma_dut_print( DUT_MSG_INFO, "Traffic stream profile %d "
				"not yet supported", s->profile);
		/* TODO */
		break;
	}

	return -1;
}


static void send_file(struct sigma_stream *s)
{
	char *pkt;
	struct timeval stop, now;
	int res;
	unsigned int counter = 0;
	int sleep_time;

	if (s->duration <= 0 || s->frame_rate < 0 || s->payload_size < 20)
		return;

	pkt = malloc(s->payload_size);
	if (pkt == NULL)
		return;
	memset(pkt, 1, s->payload_size);
	strncpy(pkt, "1345678", s->payload_size);

	gettimeofday(&stop, NULL);
	stop.tv_sec += s->duration;

	if (s->frame_rate == 0)
		sleep_time = 0;
	else {
		/* TODO: proper calibration of wait time */
		sleep_time = 1000000 / s->frame_rate;
		sleep_time -= 100;
		if (sleep_time < 0)
			sleep_time = 0;
	}

	while (!s->stop) {
		counter++;
		WPA_PUT_BE32(&pkt[8], counter);

		usleep(sleep_time);

		gettimeofday(&now, NULL);
		if (now.tv_sec > stop.tv_sec ||
		    (now.tv_sec == stop.tv_sec && now.tv_usec >= stop.tv_usec))
			break;
		WPA_PUT_BE32(&pkt[12], now.tv_sec);
		WPA_PUT_BE32(&pkt[16], now.tv_usec);

		res = send(s->sock, pkt, s->payload_size, 0);
		if (res >= 0) {
			s->tx_frames++;
			s->tx_payload_bytes += res;
		} else {
			switch (errno) {
			case EAGAIN:
			case ENOBUFS:
				usleep(1000);
				break;
			case ECONNRESET:
			case EPIPE:
				s->stop = 1;
				break;
			default:
				perror("send");
				break;
			}
		}
	}

	free(pkt);
}


static void send_transaction(struct sigma_stream *s)
{
	char *pkt, *rpkt;
	struct timeval stop, now, resp;
	int res;
	unsigned int counter = 0, rcounter;
	int wait_time;
	fd_set rfds;
	struct timeval tv;

	if (s->duration <= 0 || s->frame_rate <= 0 || s->payload_size < 20)
		return;

	pkt = malloc(s->payload_size);
	if (pkt == NULL)
		return;
	rpkt = malloc(s->payload_size);
	if (rpkt == NULL) {
		free(pkt);
		return;
	}
	memset(pkt, 1, s->payload_size);
	strncpy(pkt, "1345678", s->payload_size);

	gettimeofday(&stop, NULL);
	stop.tv_sec += s->duration;

	wait_time = 1000000 / s->frame_rate;

	while (!s->stop) {
		counter++;
		if (s->max_cnt && counter > s->max_cnt)
			break;
		WPA_PUT_BE32(&pkt[8], counter);

		gettimeofday(&now, NULL);
		if (now.tv_sec > stop.tv_sec ||
		    (now.tv_sec == stop.tv_sec && now.tv_usec >= stop.tv_usec))
			break;
		WPA_PUT_BE32(&pkt[12], now.tv_sec);
		WPA_PUT_BE32(&pkt[16], now.tv_usec);

		res = send(s->sock, pkt, s->payload_size, 0);
		if (res >= 0) {
			s->tx_frames++;
			s->tx_payload_bytes += res;
		} else {
			switch (errno) {
			case EAGAIN:
			case ENOBUFS:
				usleep(1000);
				break;
			case ECONNRESET:
			case EPIPE:
				s->stop = 1;
				break;
			default:
				perror("send");
				break;
			}
		}

		/* Wait for response */
		tv.tv_sec = 0;
		tv.tv_usec = wait_time;
		FD_ZERO(&rfds);
		FD_SET(s->sock, &rfds);
		res = select(s->sock + 1, &rfds, NULL, NULL, &tv);
		if (res < 0) {
			if (errno == EINTR)
				continue;
			perror("select");
			break;
		}

		if (res == 0) {
			/* timeout */
			continue;
		}

		if (FD_ISSET(s->sock, &rfds)) {
			/* response received */
			gettimeofday(&resp, NULL);
			res = recv(s->sock, rpkt, s->payload_size, 0);
			if (res < 0) {
				perror("recv");
				break;
			}
			rcounter = WPA_GET_BE32(&rpkt[8]);
			if (rcounter != counter)
				s->out_of_seq_frames++;
			s->rx_frames++;
			s->rx_payload_bytes += res;
#if 0
			diff = (resp.tv_sec - now.tv_sec) * 1000000 +
				resp.tv_usec - now.tv_usec;
#endif
		}
	}

	free(pkt);
	free(rpkt);
}


static void * send_thread(void *ctx)
{
	struct sigma_stream *s = ctx;

	sleep(s->start_delay);

	switch (s->profile) {
	case SIGMA_PROFILE_FILE_TRANSFER:
		send_file(s);
		break;
	case SIGMA_PROFILE_MULTICAST:
		break;
	case SIGMA_PROFILE_IPTV:
		send_file(s);
		break;
	case SIGMA_PROFILE_TRANSACTION:
		send_transaction(s);
		break;
	case SIGMA_PROFILE_START_SYNC:
		break;
	}

	return NULL;
}


static int cmd_traffic_agent_send(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	const char *val;
	int streams[MAX_SIGMA_STREAMS];
	int i, j, ret, count;
	char buf[100 + MAX_SIGMA_STREAMS * 60], *pos;

	val = get_param(cmd, "streamID");
	if (val == NULL)
		return -1;
	count = get_stream_id(val, streams);
	if (count < 0)
		return -1;
	for (i = 0; i < count; i++) {
		if (streams[i] > dut->num_streams) {
			snprintf(buf, sizeof(buf), "errorCode,StreamID %d "
				 "not configured", streams[i]);
			send_resp(dut, conn, SIGMA_INVALID, buf);
			return 0;
		}
		for (j = 0; j < i; j++)
			if (streams[i] == streams[j])
				return -1;
		if (!dut->streams[streams[i] - 1].sender) {
			snprintf(buf, sizeof(buf), "errorCode,Not configured "
				 "as sender for streamID %d", streams[i]);
			send_resp(dut, conn, SIGMA_INVALID, buf);
			return 0;
		}
	}

	for (i = 0; i < count; i++) {
		struct sigma_stream *s = &dut->streams[streams[i] - 1];
		sigma_dut_print( DUT_MSG_DEBUG, "Traffic agent: open "
				"socket for stream %d", streams[i]);
		if (open_socket(dut, s) < 0)
			return -2;
	}

	for (i = 0; i < count; i++) {
		struct sigma_stream *s = &dut->streams[streams[i] - 1];
		int res;
		sigma_dut_print( DUT_MSG_DEBUG, "Traffic agent: start "
				"send for stream %d", streams[i]);
		res = pthread_create(&s->thr, NULL, send_thread, s);
		if (res) {
			sigma_dut_print( DUT_MSG_INFO, "pthread_create "
					"failed: %d", res);
			return -2;
		}
		s->started = 1;
	}

	for (i = 0; i < count; i++) {
		sigma_dut_print( DUT_MSG_DEBUG, "Traffic agent: waiting "
				"for stream %d send to complete", streams[i]);
		stop_stream(&dut->streams[streams[i] - 1]);
	}

	buf[0] = '\0';
	pos = buf;

	pos += snprintf(pos, buf + sizeof(buf) - pos, "streamID,");
	for (i = 0; i < count; i++) {
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", streams[i]);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",txFrames,");
	for (i = 0; i < count; i++) {
		struct sigma_stream *s = &dut->streams[streams[i] - 1];
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", s->tx_frames);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",rxFrames,");
	for (i = 0; i < count; i++) {
		struct sigma_stream *s = &dut->streams[streams[i] - 1];
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", s->rx_frames);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",txPayloadBytes,");
	for (i = 0; i < count; i++) {
		struct sigma_stream *s = &dut->streams[streams[i] - 1];
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", s->tx_payload_bytes);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",rxPayloadBytes,");
	for (i = 0; i < count; i++) {
		struct sigma_stream *s = &dut->streams[streams[i] - 1];
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", s->rx_payload_bytes);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",outOfSequenceFrames,");
	for (i = 0; i < count; i++) {
		struct sigma_stream *s = &dut->streams[streams[i] - 1];
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", s->out_of_seq_frames);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	buf[sizeof(buf) - 1] = '\0';

	send_resp(dut, conn, SIGMA_COMPLETE, buf);

	return 0;
}


static void receive_file(struct sigma_stream *s)
{
	struct timeval tv;
	fd_set rfds;
	int res;
	char *pkt;
	int pktlen;
	unsigned int last_rx = 0, counter;

	pktlen = 65536 + 1;
	pkt = malloc(pktlen);
	if (pkt == NULL)
		return;

	while (!s->stop) {
		FD_ZERO(&rfds);
		FD_SET(s->sock, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 300000;
		res = select(s->sock + 1, &rfds, NULL, NULL, &tv);
		if (res < 0) {
			perror("select");
			usleep(10000);
		} else if (FD_ISSET(s->sock, &rfds)) {
			res = recv(s->sock, pkt, pktlen, 0);
			if (res >= 0) {
				s->rx_frames++;
				s->rx_payload_bytes += res;

				counter = WPA_GET_BE32(&pkt[8]);
				if (counter < last_rx)
					s->out_of_seq_frames++;
				last_rx = counter;
			} else {
				perror("recv");
				break;
			}
		}
	}

	free(pkt);
}


static void receive_transaction(struct sigma_stream *s)
{
	struct timeval tv;
	fd_set rfds;
	int res;
	char *pkt;
	int pktlen;
	unsigned int last_rx = 0, counter;
	struct sockaddr_in addr;
	socklen_t addrlen;

	if (s->payload_size)
		pktlen = s->payload_size;
	else
		pktlen = 65536 + 1;
	pkt = malloc(pktlen);
	if (pkt == NULL)
		return;

	while (!s->stop) {
		FD_ZERO(&rfds);
		FD_SET(s->sock, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 300000;
		res = select(s->sock + 1, &rfds, NULL, NULL, &tv);
		if (res < 0) {
			perror("select");
			usleep(10000);
		} else if (FD_ISSET(s->sock, &rfds)) {
			addrlen = sizeof(addr);
			res = recvfrom(s->sock, pkt, pktlen, 0,
				       (struct sockaddr *) &addr, &addrlen);
			if (res < 0) {
				perror("recv");
				break;
			}

			s->rx_frames++;
			s->rx_payload_bytes += res;

			counter = WPA_GET_BE32(&pkt[8]);
			if (counter < last_rx)
				s->out_of_seq_frames++;
			last_rx = counter;

			/* send response */
			res = sendto(s->sock, pkt, pktlen, 0,
				     (struct sockaddr *) &addr, addrlen);
			if (res < 0) {
				perror("sendto");
			} else {
				s->tx_frames++;
				s->tx_payload_bytes += res;
			}
		}
	}

	free(pkt);
}


static void * receive_thread(void *ctx)
{
	struct sigma_stream *s = ctx;

	switch (s->profile) {
	case SIGMA_PROFILE_FILE_TRANSFER:
		receive_file(s);
		break;
	case SIGMA_PROFILE_MULTICAST:
		break;
	case SIGMA_PROFILE_IPTV:
		receive_file(s);
		break;
	case SIGMA_PROFILE_TRANSACTION:
		receive_transaction(s);
		break;
	case SIGMA_PROFILE_START_SYNC:
		break;
	}

	return NULL;
}


static int cmd_traffic_agent_receive_start(struct sigma_dut *dut,
					   struct sigma_conn *conn,
					   struct sigma_cmd *cmd)
{
	const char *val;
	int streams[MAX_SIGMA_STREAMS];
	int i, j, count;
	char buf[100];

	val = get_param(cmd, "streamID");
	if (val == NULL)
		return -1;
	count = get_stream_id(val, streams);
	if (count < 0)
		return -1;
	for (i = 0; i < count; i++) {
		if (streams[i] > dut->num_streams) {
			snprintf(buf, sizeof(buf), "errorCode,StreamID %d "
				 "not configured", streams[i]);
			send_resp(dut, conn, SIGMA_INVALID, buf);
			return 0;
		}
		for (j = 0; j < i; j++)
			if (streams[i] == streams[j])
				return -1;
		if (dut->streams[streams[i] - 1].sender) {
			snprintf(buf, sizeof(buf), "errorCode,Not configured "
				 "as receiver for streamID %d", streams[i]);
			send_resp(dut, conn, SIGMA_INVALID, buf);
			return 0;
		}
	}

	for (i = 0; i < count; i++) {
		struct sigma_stream *s = &dut->streams[streams[i] - 1];
		sigma_dut_print( DUT_MSG_DEBUG, "Traffic agent: open "
				"receive socket for stream %d", streams[i]);
		if (open_socket(dut, s) < 0)
			return -2;
	}

	for (i = 0; i < count; i++) {
		struct sigma_stream *s = &dut->streams[streams[i] - 1];
		int res;
		sigma_dut_print( DUT_MSG_DEBUG, "Traffic agent: start "
				"receive for stream %d", streams[i]);
		res = pthread_create(&s->thr, NULL, receive_thread, s);
		if (res) {
			sigma_dut_print( DUT_MSG_INFO, "pthread_create "
					"failed: %d", res);
			return -2;
		}
		s->started = 1;
	}

	return 1;
}


static int cmd_traffic_agent_receive_stop(struct sigma_dut *dut,
					  struct sigma_conn *conn,
					  struct sigma_cmd *cmd)
{
	const char *val;
	int streams[MAX_SIGMA_STREAMS];
	int i, j, ret, count;
	char buf[100 + MAX_SIGMA_STREAMS * 60], *pos;

	val = get_param(cmd, "streamID");
	if (val == NULL)
		return -1;
	count = get_stream_id(val, streams);
	if (count < 0)
		return -1;
	for (i = 0; i < count; i++) {
		if (streams[i] > dut->num_streams) {
			snprintf(buf, sizeof(buf), "errorCode,StreamID %d "
				 "not configured", streams[i]);
			send_resp(dut, conn, SIGMA_INVALID, buf);
			return 0;
		}
		for (j = 0; j < i; j++)
			if (streams[i] == streams[j])
				return -1;
		if (!dut->streams[streams[i] - 1].started) {
			snprintf(buf, sizeof(buf), "errorCode,Receive not "
				 "started for streamID %d", streams[i]);
			send_resp(dut, conn, SIGMA_INVALID, buf);
			return 0;
		}
	}

	for (i = 0; i < count; i++) {
		struct sigma_stream *s = &dut->streams[streams[i] - 1];
		s->stop = 1;
	}

	for (i = 0; i < count; i++) {
		struct sigma_stream *s = &dut->streams[streams[i] - 1];
		sigma_dut_print( DUT_MSG_DEBUG, "Traffic agent: stop "
				"receive for stream %d", streams[i]);
		stop_stream(s);
	}

	buf[0] = '\0';
	pos = buf;

	pos += snprintf(pos, buf + sizeof(buf) - pos, "streamID,");
	for (i = 0; i < count; i++) {
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", streams[i]);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",txFrames,");
	for (i = 0; i < count; i++) {
		struct sigma_stream *s = &dut->streams[streams[i] - 1];
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", s->tx_frames);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",rxFrames,");
	for (i = 0; i < count; i++) {
		struct sigma_stream *s = &dut->streams[streams[i] - 1];
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", s->rx_frames);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",txPayloadBytes,");
	for (i = 0; i < count; i++) {
		struct sigma_stream *s = &dut->streams[streams[i] - 1];
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", s->tx_payload_bytes);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",rxPayloadBytes,");
	for (i = 0; i < count; i++) {
		struct sigma_stream *s = &dut->streams[streams[i] - 1];
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", s->rx_payload_bytes);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	pos += snprintf(pos, buf + sizeof(buf) - pos, ",outOfSequenceFrames,");
	for (i = 0; i < count; i++) {
		struct sigma_stream *s = &dut->streams[streams[i] - 1];
		ret = snprintf(pos, buf + sizeof(buf) - pos, "%s%d",
			       i > 0 ? " " : "", s->out_of_seq_frames);
		if (ret < 0 || ret >= buf + sizeof(buf) - pos)
			break;
		pos += ret;
	}

	buf[sizeof(buf) - 1] = '\0';

	send_resp(dut, conn, SIGMA_COMPLETE, buf);

	return 0;
}


void traffic_agent_register_cmds(void)
{
	sigma_dut_reg_cmd("traffic_agent_config", NULL,
			  cmd_traffic_agent_config);
	sigma_dut_reg_cmd("traffic_agent_reset", NULL,
			  cmd_traffic_agent_reset);
	sigma_dut_reg_cmd("traffic_agent_send", NULL,
			  cmd_traffic_agent_send);
	sigma_dut_reg_cmd("traffic_agent_receive_start", NULL,
			  cmd_traffic_agent_receive_start);
	sigma_dut_reg_cmd("traffic_agent_receive_stop", NULL,
			  cmd_traffic_agent_receive_stop);
}
