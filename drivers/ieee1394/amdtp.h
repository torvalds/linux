/* -*- c-basic-offset: 8 -*- */

#ifndef __AMDTP_H
#define __AMDTP_H

#include <asm/types.h>
#include "ieee1394-ioctl.h"

/* The userspace interface for the Audio & Music Data Transmission
 * Protocol driver is really simple. First, open /dev/amdtp, use the
 * ioctl to configure format, rate, dimension and either plug or
 * channel, then start writing samples.
 *
 * The formats supported by the driver are listed below.
 * AMDTP_FORMAT_RAW corresponds to the AM824 raw format, which can
 * carry any number of channels, so use this if you're streaming
 * multichannel audio.  The AMDTP_FORMAT_IEC958_PCM corresponds to the
 * AM824 IEC958 encapsulation without the IEC958 data bit set, using
 * AMDTP_FORMAT_IEC958_AC3 will transmit the samples with the data bit
 * set, suitable for transmitting compressed AC-3 audio.
 *
 * The rate field specifies the transmission rate; supported values
 * are 32000, 44100, 48000, 88200, 96000, 176400 and 192000.
 *
 * The dimension field specifies the dimension of the signal, that is,
 * the number of audio channels.  Only AMDTP_FORMAT_RAW supports
 * settings greater than 2.
 *
 * The mode field specifies which transmission mode to use. The AMDTP
 * specifies two different transmission modes: blocking and
 * non-blocking.  The blocking transmission mode always send a fixed
 * number of samples, typically 8, 16 or 32.  To exactly match the
 * transmission rate, the driver alternates between sending empty and
 * non-empty packets.  In non-blocking mode, the driver transmits as
 * small packets as possible.  For example, for a transmission rate of
 * 44100Hz, the driver should send 5 41/80 samples in every cycle, but
 * this is not possible so instead the driver alternates between
 * sending 5 and 6 samples.
 *
 * The last thing to specify is either the isochronous channel to use
 * or the output plug to connect to.  If you know what channel the
 * destination device will listen on, you can specify the channel
 * directly and use the AMDTP_IOC_CHANNEL ioctl.  However, if the
 * destination device chooses the channel and uses the IEC61883-1 plug
 * mechanism, you can specify an output plug to connect to.  The
 * driver will pick up the channel number from the plug once the
 * destination device locks the output plug control register.  In this
 * case set the plug field and use the AMDTP_IOC_PLUG ioctl.
 *
 * Having configured the interface, the driver now accepts writes of
 * regular 16 bit signed little endian samples, with the channels
 * interleaved.  For example, 4 channels would look like:
 *
 *   | sample 0                                      | sample 1    ...
 *   | ch. 0     | ch. 1     | ch. 2     | ch. 3     | ch. 0     | ...
 *   | lsb | msb | lsb | msb | lsb | msb | lsb | msb | lsb | msb | ...
 *
 */

enum {
	AMDTP_FORMAT_RAW,
	AMDTP_FORMAT_IEC958_PCM,
	AMDTP_FORMAT_IEC958_AC3
};

enum {
	AMDTP_MODE_BLOCKING,
	AMDTP_MODE_NON_BLOCKING,
};

enum {
	AMDTP_INPUT_LE16,
	AMDTP_INPUT_BE16,
};

struct amdtp_ioctl {
	__u32 format;
	__u32 rate;
	__u32 dimension;
	__u32 mode;
	union { __u32 channel; __u32 plug; } u;
};

#endif /* __AMDTP_H */
