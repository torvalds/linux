Cadence IP BRA support
----------------------

Format requirements
~~~~~~~~~~~~~~~~~~~

The Cadence IP relies on PDI0 for TX and PDI1 for RX. The data needs
to be formatted with the following conventions:

  (1) all Data is stored in bits 15..0 of the 32-bit PDI FIFOs.

  (2) the start of packet is BIT(31).

  (3) the end of packet is BIT(30).

  (4) A packet ID is stored in bits 19..16. This packet ID is
      determined by software and is typically a rolling counter.

  (5) Padding shall be inserted as needed so that the Header CRC,
      Header response, Footer CRC, Footer response are always in
      Byte0. Padding is inserted by software for writes, and on reads
      software shall discard the padding added by the hardware.

Example format
~~~~~~~~~~~~~~

The following table represents the sequence provided to PDI0 for a
write command followed by a read command.

::

	+---+---+--------+---------------+---------------+
	+ 1 | 0 | ID = 0 |  WR HDR[1]    |  WR HDR[0]    |
	+   |   |        |  WR HDR[3]    |  WR HDR[2]    |
	+   |   |        |  WR HDR[5]    |  WR HDR[4]    |
	+   |   |        |  pad          |  WR HDR CRC   |
	+   |   |        |  WR Data[1]   |  WR Data[0]   |
	+   |   |        |  WR Data[3]   |  WR Data[2]   |
	+   |   |        |  WR Data[n-2] |  WR Data[n-3] |
	+   |   |        |  pad          |  WR Data[n-1] |
	+ 0 | 1 |        |  pad          |  WR Data CRC  |
	+---+---+--------+---------------+---------------+
	+ 1 | 0 | ID = 1 |  RD HDR[1]    |  RD HDR[0]    |
	+   |   |        |  RD HDR[3]    |  RD HDR[2]    |
	+   |   |        |  RD HDR[5]    |  RD HDR[4]    |
	+ 0 | 1 |        |  pad          |  RD HDR CRC   |
	+---+---+--------+---------------+---------------+


The table below represents the data received on PDI1 for the same
write command followed by a read command.

::

	+---+---+--------+---------------+---------------+
	+ 1 | 0 | ID = 0 |  pad          |  WR Hdr Rsp   |
	+ 0 | 1 |        |  pad          |  WR Ftr Rsp   |
	+---+---+--------+---------------+---------------+
	+ 1 | 0 | ID = 0 |  pad          |  Rd Hdr Rsp   |
	+   |   |        |  RD Data[1]   |  RD Data[0]   |
	+   |   |        |  RD Data[3]   |  RD Data[2]   |
	+   |   |        |  RD HDR[n-2]  |  RD Data[n-3] |
	+   |   |        |  pad          |  RD Data[n-1] |
	+   |   |        |  pad          |  RD Data CRC  |
	+ 0 | 1 |        |  pad          |  RD Ftr Rsp   |
	+---+---+--------+---------------+---------------+
