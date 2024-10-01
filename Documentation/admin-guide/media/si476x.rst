.. SPDX-License-Identifier: GPL-2.0

.. include:: <isonum.txt>


The SI476x Driver
=================

Copyright |copy| 2013 Andrey Smirnov <andrew.smirnov@gmail.com>

TODO for the driver
-------------------

- According to the SiLabs' datasheet it is possible to update the
  firmware of the radio chip in the run-time, thus bringing it to the
  most recent version. Unfortunately I couldn't find any mentioning of
  the said firmware update for the old chips that I tested the driver
  against, so for chips like that the driver only exposes the old
  functionality.


Parameters exposed over debugfs
-------------------------------
SI476x allow user to get multiple characteristics that can be very
useful for EoL testing/RF performance estimation, parameters that have
very little to do with V4L2 subsystem. Such parameters are exposed via
debugfs and can be accessed via regular file I/O operations.

The drivers exposes following files:

* /sys/kernel/debug/<device-name>/acf
  This file contains ACF(Automatically Controlled Features) status
  information. The contents of the file is binary data of the
  following layout:

  .. tabularcolumns:: |p{7ex}|p{12ex}|L|

  =============  ==============   ====================================
  Offset	 Name		  Description
  =============  ==============   ====================================
  0x00		 blend_int	  Flag, set when stereo separation has
				  crossed below the blend threshold
  0x01		 hblend_int	  Flag, set when HiBlend cutoff
				  frequency is lower than threshold
  0x02		 hicut_int	  Flag, set when HiCut cutoff
				  frequency is lower than threshold
  0x03		 chbw_int	  Flag, set when channel filter
				  bandwidth is less than threshold
  0x04		 softmute_int	  Flag indicating that softmute
				  attenuation has increased above
				  softmute threshold
  0x05		 smute		  0 - Audio is not soft muted
				  1 - Audio is soft muted
  0x06		 smattn		  Soft mute attenuation level in dB
  0x07		 chbw		  Channel filter bandwidth in kHz
  0x08		 hicut		  HiCut cutoff frequency in units of
				  100Hz
  0x09		 hiblend	  HiBlend cutoff frequency in units
				  of 100 Hz
  0x10		 pilot		  0 - Stereo pilot is not present
				  1 - Stereo pilot is present
  0x11		 stblend	  Stereo blend in %
  =============  ==============   ====================================


* /sys/kernel/debug/<device-name>/rds_blckcnt
  This file contains statistics about RDS receptions. It's binary data
  has the following layout:

  .. tabularcolumns:: |p{7ex}|p{12ex}|L|

  =============  ==============   ====================================
  Offset	 Name		  Description
  =============  ==============   ====================================
  0x00		 expected	  Number of expected RDS blocks
  0x02		 received	  Number of received RDS blocks
  0x04		 uncorrectable	  Number of uncorrectable RDS blocks
  =============  ==============   ====================================

* /sys/kernel/debug/<device-name>/agc
  This file contains information about parameters pertaining to
  AGC(Automatic Gain Control)

  The layout is:

  .. tabularcolumns:: |p{7ex}|p{12ex}|L|

  =============  ==============   ====================================
  Offset	 Name		  Description
  =============  ==============   ====================================
  0x00		 mxhi		  0 - FM Mixer PD high threshold is
				  not tripped
				  1 - FM Mixer PD high threshold is
				  tripped
  0x01		 mxlo		  ditto for FM Mixer PD low
  0x02		 lnahi		  ditto for FM LNA PD high
  0x03		 lnalo		  ditto for FM LNA PD low
  0x04		 fmagc1		  FMAGC1 attenuator resistance
				  (see datasheet for more detail)
  0x05		 fmagc2		  ditto for FMAGC2
  0x06		 pgagain	  PGA gain in dB
  0x07		 fmwblang	  FM/WB LNA Gain in dB
  =============  ==============   ====================================

* /sys/kernel/debug/<device-name>/rsq
  This file contains information about parameters pertaining to
  RSQ(Received Signal Quality)

  The layout is:

  .. tabularcolumns:: |p{7ex}|p{12ex}|p{60ex}|

  =============  ==============   ====================================
  Offset	 Name		  Description
  =============  ==============   ====================================
  0x00		 multhint	  0 - multipath value has not crossed
				  the Multipath high threshold
				  1 - multipath value has crossed
				  the Multipath high threshold
  0x01		 multlint	  ditto for Multipath low threshold
  0x02		 snrhint	  0 - received signal's SNR has not
				  crossed high threshold
				  1 - received signal's SNR has
				  crossed high threshold
  0x03		 snrlint	  ditto for low threshold
  0x04		 rssihint	  ditto for RSSI high threshold
  0x05		 rssilint	  ditto for RSSI low threshold
  0x06		 bltf		  Flag indicating if seek command
				  reached/wrapped seek band limit
  0x07		 snr_ready	  Indicates that SNR metrics is ready
  0x08		 rssiready	  ditto for RSSI metrics
  0x09		 injside	  0 - Low-side injection is being used
				  1 - High-side injection is used
  0x10		 afcrl		  Flag indicating if AFC rails
  0x11		 valid		  Flag indicating if channel is valid
  0x12		 readfreq	  Current tuned frequency
  0x14		 freqoff	  Signed frequency offset in units of
				  2ppm
  0x15		 rssi		  Signed value of RSSI in dBuV
  0x16		 snr		  Signed RF SNR in dB
  0x17		 issi		  Signed Image Strength Signal
				  indicator
  0x18		 lassi		  Signed Low side adjacent Channel
				  Strength indicator
  0x19		 hassi		  ditto fpr High side
  0x20		 mult		  Multipath indicator
  0x21		 dev		  Frequency deviation
  0x24		 assi		  Adjacent channel SSI
  0x25		 usn		  Ultrasonic noise indicator
  0x26		 pilotdev	  Pilot deviation in units of 100 Hz
  0x27		 rdsdev		  ditto for RDS
  0x28		 assidev	  ditto for ASSI
  0x29		 strongdev	  Frequency deviation
  0x30		 rdspi		  RDS PI code
  =============  ==============   ====================================

* /sys/kernel/debug/<device-name>/rsq_primary
  This file contains information about parameters pertaining to
  RSQ(Received Signal Quality) for primary tuner only. Layout is as
  the one above.
