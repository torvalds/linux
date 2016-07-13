.. -*- coding: utf-8; mode: rst -*-

.. _dvb_examples:

********
Examples
********

In this section we would like to present some examples for using the DVB
API.

..note:: This section is out of date, and the code below won't even
   compile. Please refer to the
   `libdvbv5 <https://linuxtv.org/docs/libdvbv5/index.html>`__ for
   updated/recommended examples.


.. _tuning:

Example: Tuning
===============

We will start with a generic tuning subroutine that uses the frontend
and SEC, as well as the demux devices. The example is given for QPSK
tuners, but can easily be adjusted for QAM.


.. code-block:: c

     #include <sys/ioctl.h>
     #include <stdio.h>
     #include <stdint.h>
     #include <sys/types.h>
     #include <sys/stat.h>
     #include <fcntl.h>
     #include <time.h>
     #include <unistd.h>

     #include <linux/dvb/dmx.h>
     #include <linux/dvb/frontend.h>
     #include <linux/dvb/sec.h>
     #include <sys/poll.h>

     #define DMX "/dev/dvb/adapter0/demux1"
     #define FRONT "/dev/dvb/adapter0/frontend1"
     #define SEC "/dev/dvb/adapter0/sec1"

     /* routine for checking if we have a signal and other status information*/
     int FEReadStatus(int fd, fe_status_t *stat)
     {
	 int ans;

	 if ( (ans = ioctl(fd,FE_READ_STATUS,stat) < 0)){
	     perror("FE READ STATUS: ");
	     return -1;
	 }

	 if (*stat & FE_HAS_POWER)
	     printf("FE HAS POWER\\n");

	 if (*stat & FE_HAS_SIGNAL)
	     printf("FE HAS SIGNAL\\n");

	 if (*stat & FE_SPECTRUM_INV)
	     printf("SPEKTRUM INV\\n");

	 return 0;
     }


     /* tune qpsk */
     /* freq:             frequency of transponder                      */
     /* vpid, apid, tpid: PIDs of video, audio and teletext TS packets  */
     /* diseqc:           DiSEqC address of the used LNB                */
     /* pol:              Polarisation                                  */
     /* srate:            Symbol Rate                                   */
     /* fec.              FEC                                           */
     /* lnb_lof1:         local frequency of lower LNB band             */
     /* lnb_lof2:         local frequency of upper LNB band             */
     /* lnb_slof:         switch frequency of LNB                       */

     int set_qpsk_channel(int freq, int vpid, int apid, int tpid,
	     int diseqc, int pol, int srate, int fec, int lnb_lof1,
	     int lnb_lof2, int lnb_slof)
     {
	 struct secCommand scmd;
	 struct secCmdSequence scmds;
	 struct dmx_pes_filter_params pesFilterParams;
	 FrontendParameters frp;
	 struct pollfd pfd[1];
	 FrontendEvent event;
	 int demux1, demux2, demux3, front;

	 frequency = (uint32_t) freq;
	 symbolrate = (uint32_t) srate;

	 if((front = open(FRONT,O_RDWR)) < 0){
	     perror("FRONTEND DEVICE: ");
	     return -1;
	 }

	 if((sec = open(SEC,O_RDWR)) < 0){
	     perror("SEC DEVICE: ");
	     return -1;
	 }

	 if (demux1 < 0){
	     if ((demux1=open(DMX, O_RDWR|O_NONBLOCK))
		 < 0){
		 perror("DEMUX DEVICE: ");
		 return -1;
	     }
	 }

	 if (demux2 < 0){
	     if ((demux2=open(DMX, O_RDWR|O_NONBLOCK))
		 < 0){
		 perror("DEMUX DEVICE: ");
		 return -1;
	     }
	 }

	 if (demux3 < 0){
	     if ((demux3=open(DMX, O_RDWR|O_NONBLOCK))
		 < 0){
		 perror("DEMUX DEVICE: ");
		 return -1;
	     }
	 }

	 if (freq < lnb_slof) {
	     frp.Frequency = (freq - lnb_lof1);
	     scmds.continuousTone = SEC_TONE_OFF;
	 } else {
	     frp.Frequency = (freq - lnb_lof2);
	     scmds.continuousTone = SEC_TONE_ON;
	 }
	 frp.Inversion = INVERSION_AUTO;
	 if (pol) scmds.voltage = SEC_VOLTAGE_18;
	 else scmds.voltage = SEC_VOLTAGE_13;

	 scmd.type=0;
	 scmd.u.diseqc.addr=0x10;
	 scmd.u.diseqc.cmd=0x38;
	 scmd.u.diseqc.numParams=1;
	 scmd.u.diseqc.params[0] = 0xF0 | ((diseqc * 4) & 0x0F) |
	     (scmds.continuousTone == SEC_TONE_ON ? 1 : 0) |
	     (scmds.voltage==SEC_VOLTAGE_18 ? 2 : 0);

	 scmds.miniCommand=SEC_MINI_NONE;
	 scmds.numCommands=1;
	 scmds.commands=&scmd;
	 if (ioctl(sec, SEC_SEND_SEQUENCE, &scmds) < 0){
	     perror("SEC SEND: ");
	     return -1;
	 }

	 if (ioctl(sec, SEC_SEND_SEQUENCE, &scmds) < 0){
	     perror("SEC SEND: ");
	     return -1;
	 }

	 frp.u.qpsk.SymbolRate = srate;
	 frp.u.qpsk.FEC_inner = fec;

	 if (ioctl(front, FE_SET_FRONTEND, &frp) < 0){
	     perror("QPSK TUNE: ");
	     return -1;
	 }

	 pfd[0].fd = front;
	 pfd[0].events = POLLIN;

	 if (poll(pfd,1,3000)){
	     if (pfd[0].revents & POLLIN){
		 printf("Getting QPSK event\\n");
		 if ( ioctl(front, FE_GET_EVENT, &event)

		      == -EOVERFLOW){
		     perror("qpsk get event");
		     return -1;
		 }
		 printf("Received ");
		 switch(event.type){
		 case FE_UNEXPECTED_EV:
		     printf("unexpected event\\n");
		     return -1;
		 case FE_FAILURE_EV:
		     printf("failure event\\n");
		     return -1;

		 case FE_COMPLETION_EV:
		     printf("completion event\\n");
		 }
	     }
	 }


	 pesFilterParams.pid     = vpid;
	 pesFilterParams.input   = DMX_IN_FRONTEND;
	 pesFilterParams.output  = DMX_OUT_DECODER;
	 pesFilterParams.pes_type = DMX_PES_VIDEO;
	 pesFilterParams.flags   = DMX_IMMEDIATE_START;
	 if (ioctl(demux1, DMX_SET_PES_FILTER, &pesFilterParams) < 0){
	     perror("set_vpid");
	     return -1;
	 }

	 pesFilterParams.pid     = apid;
	 pesFilterParams.input   = DMX_IN_FRONTEND;
	 pesFilterParams.output  = DMX_OUT_DECODER;
	 pesFilterParams.pes_type = DMX_PES_AUDIO;
	 pesFilterParams.flags   = DMX_IMMEDIATE_START;
	 if (ioctl(demux2, DMX_SET_PES_FILTER, &pesFilterParams) < 0){
	     perror("set_apid");
	     return -1;
	 }

	 pesFilterParams.pid     = tpid;
	 pesFilterParams.input   = DMX_IN_FRONTEND;
	 pesFilterParams.output  = DMX_OUT_DECODER;
	 pesFilterParams.pes_type = DMX_PES_TELETEXT;
	 pesFilterParams.flags   = DMX_IMMEDIATE_START;
	 if (ioctl(demux3, DMX_SET_PES_FILTER, &pesFilterParams) < 0){
	     perror("set_tpid");
	     return -1;
	 }

	 return has_signal(fds);
     }

The program assumes that you are using a universal LNB and a standard
DiSEqC switch with up to 4 addresses. Of course, you could build in some
more checking if tuning was successful and maybe try to repeat the
tuning process. Depending on the external hardware, i.e. LNB and DiSEqC
switch, and weather conditions this may be necessary.


.. _the_dvr_device:

Example: The DVR device
========================

The following program code shows how to use the DVR device for
recording.


.. code-block:: c

     #include <sys/ioctl.h>
     #include <stdio.h>
     #include <stdint.h>
     #include <sys/types.h>
     #include <sys/stat.h>
     #include <fcntl.h>
     #include <time.h>
     #include <unistd.h>

     #include <linux/dvb/dmx.h>
     #include <linux/dvb/video.h>
     #include <sys/poll.h>
     #define DVR "/dev/dvb/adapter0/dvr1"
     #define AUDIO "/dev/dvb/adapter0/audio1"
     #define VIDEO "/dev/dvb/adapter0/video1"

     #define BUFFY (188*20)
     #define MAX_LENGTH (1024*1024*5) /* record 5MB */


     /* switch the demuxes to recording, assuming the transponder is tuned */

     /* demux1, demux2: file descriptor of video and audio filters */
     /* vpid, apid:     PIDs of video and audio channels           */

     int switch_to_record(int demux1, int demux2, uint16_t vpid, uint16_t apid)
     {
	 struct dmx_pes_filter_params pesFilterParams;

	 if (demux1 < 0){
	     if ((demux1=open(DMX, O_RDWR|O_NONBLOCK))
		 < 0){
		 perror("DEMUX DEVICE: ");
		 return -1;
	     }
	 }

	 if (demux2 < 0){
	     if ((demux2=open(DMX, O_RDWR|O_NONBLOCK))
		 < 0){
		 perror("DEMUX DEVICE: ");
		 return -1;
	     }
	 }

	 pesFilterParams.pid = vpid;
	 pesFilterParams.input = DMX_IN_FRONTEND;
	 pesFilterParams.output = DMX_OUT_TS_TAP;
	 pesFilterParams.pes_type = DMX_PES_VIDEO;
	 pesFilterParams.flags = DMX_IMMEDIATE_START;
	 if (ioctl(demux1, DMX_SET_PES_FILTER, &pesFilterParams) < 0){
	     perror("DEMUX DEVICE");
	     return -1;
	 }
	 pesFilterParams.pid = apid;
	 pesFilterParams.input = DMX_IN_FRONTEND;
	 pesFilterParams.output = DMX_OUT_TS_TAP;
	 pesFilterParams.pes_type = DMX_PES_AUDIO;
	 pesFilterParams.flags = DMX_IMMEDIATE_START;
	 if (ioctl(demux2, DMX_SET_PES_FILTER, &pesFilterParams) < 0){
	     perror("DEMUX DEVICE");
	     return -1;
	 }
	 return 0;
     }

     /* start recording MAX_LENGTH , assuming the transponder is tuned */

     /* demux1, demux2: file descriptor of video and audio filters */
     /* vpid, apid:     PIDs of video and audio channels           */
     int record_dvr(int demux1, int demux2, uint16_t vpid, uint16_t apid)
     {
	 int i;
	 int len;
	 int written;
	 uint8_t buf[BUFFY];
	 uint64_t length;
	 struct pollfd pfd[1];
	 int dvr, dvr_out;

	 /* open dvr device */
	 if ((dvr = open(DVR, O_RDONLY|O_NONBLOCK)) < 0){
		 perror("DVR DEVICE");
		 return -1;
	 }

	 /* switch video and audio demuxes to dvr */
	 printf ("Switching dvr on\\n");
	 i = switch_to_record(demux1, demux2, vpid, apid);
	 printf("finished: ");

	 printf("Recording %2.0f MB of test file in TS format\\n",
	    MAX_LENGTH/(1024.0*1024.0));
	 length = 0;

	 /* open output file */
	 if ((dvr_out = open(DVR_FILE,O_WRONLY|O_CREAT
		      |O_TRUNC, S_IRUSR|S_IWUSR
		      |S_IRGRP|S_IWGRP|S_IROTH|
		      S_IWOTH)) < 0){
	     perror("Can't open file for dvr test");
	     return -1;
	 }

	 pfd[0].fd = dvr;
	 pfd[0].events = POLLIN;

	 /* poll for dvr data and write to file */
	 while (length < MAX_LENGTH ) {
	     if (poll(pfd,1,1)){
		 if (pfd[0].revents & POLLIN){
		     len = read(dvr, buf, BUFFY);
		     if (len < 0){
			 perror("recording");
			 return -1;
		     }
		     if (len > 0){
			 written = 0;
			 while (written < len)
			     written +=
				 write (dvr_out,
				    buf, len);
			 length += len;
			 printf("written %2.0f MB\\r",
			    length/1024./1024.);
		     }
		 }
	     }
	 }
	 return 0;
     }
