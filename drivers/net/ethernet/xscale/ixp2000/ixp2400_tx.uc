/*
 * TX ucode for the Intel IXP2400 in POS-PHY mode.
 * Copyright (C) 2004, 2005 Lennert Buytenhek
 * Dedicated to Marija Kulikova.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Assumptions made in this code:
 * - The IXP2400 MSF is configured for POS-PHY mode, in a mode where
 *   only one TBUF partition is used.  This includes, for example,
 *   1x32 SPHY and 1x32 MPHY32, but not 4x8 SPHY or 1x32 MPHY4. (This
 *   is not an exhaustive list.)
 * - The TBUF uses 64-byte mpackets.
 * - TX descriptors reside in SRAM, and have the following format:
 *	struct tx_desc
 *	{
 *	// to uengine
 *		u32	buf_phys_addr;
 *		u32	pkt_length;
 *		u32	channel;
 *	};
 * - Packet data resides in DRAM.
 * - Packet buffer addresses are 8-byte aligned.
 * - Scratch ring 2 is tx_pending.
 * - Scratch ring 3 is tx_done, and has status condition 'full'.
 * - This code is run on all eight threads of the microengine it runs on.
 */

#define TX_SEQUENCE_0		0x0060
#define TBUF_CTRL		0x1800

#define PARTITION_SIZE		128
#define PARTITION_THRESH	96


	.sig volatile sig1
	.sig volatile sig2
	.sig volatile sig3

	.reg @old_tx_seq_0
	.reg @mpkts_in_flight
	.reg @next_tbuf_mpacket

	.reg @buffer_handle
	.reg @buffer_start
	.reg @packet_length
	.reg @channel
	.reg @packet_offset

	.reg zero

	immed[zero, 0]

	/*
	 * Skip context 0 initialisation?
	 */
	.begin
		br!=ctx[0, mpacket_tx_loop#]
	.end

	/*
	 * Wait until all pending TBUF elements have been transmitted.
	 */
	.begin
		.reg read $tx
		.sig zzz

	loop_empty#:
		msf[read, $tx, zero, TX_SEQUENCE_0, 1], ctx_swap[zzz]
		alu_shf[--, --, b, $tx, >>31]
		beq[loop_empty#]

		alu[@old_tx_seq_0, --, b, $tx]
	.end

	immed[@mpkts_in_flight, 0]
	alu[@next_tbuf_mpacket, @old_tx_seq_0, and, (PARTITION_SIZE - 1)]

	immed[@buffer_handle, 0]

	/*
	 * Initialise signal pipeline.
	 */
	.begin
		local_csr_wr[SAME_ME_SIGNAL, (&sig1 << 3)]
		.set_sig sig1

		local_csr_wr[SAME_ME_SIGNAL, (&sig2 << 3)]
		.set_sig sig2

		local_csr_wr[SAME_ME_SIGNAL, (&sig3 << 3)]
		.set_sig sig3
	.end

mpacket_tx_loop#:
	.begin
		.reg tbuf_element_index
		.reg buffer_handle
		.reg sop_eop
		.reg packet_data
		.reg channel
		.reg mpacket_size

		/*
		 * If there is no packet currently being transmitted,
		 * dequeue the next TX descriptor, and fetch the buffer
		 * address, packet length and destination channel number.
		 */
		.begin
			.reg read $stemp $stemp2 $stemp3
			.xfer_order $stemp $stemp2 $stemp3
			.sig zzz

			ctx_arb[sig1]

			alu[--, --, b, @buffer_handle]
			bne[already_got_packet#]

		tx_nobufs#:
			scratch[get, $stemp, zero, 8, 1], ctx_swap[zzz]
			alu[@buffer_handle, --, b, $stemp]
			beq[tx_nobufs#]

			sram[read, $stemp, $stemp, 0, 3], ctx_swap[zzz]
			alu[@buffer_start, --, b, $stemp]
			alu[@packet_length, --, b, $stemp2]
			beq[zero_byte_packet#]
			alu[@channel, --, b, $stemp3]
			immed[@packet_offset, 0]

		already_got_packet#:
			local_csr_wr[SAME_ME_SIGNAL, (0x80 | (&sig1 << 3))]
		.end

		/*
		 * Determine tbuf element index, SOP/EOP flags, mpacket
		 * offset and mpacket size and cache buffer_handle and
		 * channel number.
		 */
		.begin
			alu[tbuf_element_index, --, b, @next_tbuf_mpacket]
			alu[@next_tbuf_mpacket, @next_tbuf_mpacket, +, 1]
			alu[@next_tbuf_mpacket, @next_tbuf_mpacket, and,
							(PARTITION_SIZE - 1)]

			alu[buffer_handle, --, b, @buffer_handle]
			immed[@buffer_handle, 0]

			immed[sop_eop, 1]

			alu[packet_data, --, b, @packet_offset]
			bne[no_sop#]
			alu[sop_eop, sop_eop, or, 2]
		no_sop#:
			alu[packet_data, packet_data, +, @buffer_start]

			alu[channel, --, b, @channel]

			alu[mpacket_size, @packet_length, -, @packet_offset]
			alu[--, 64, -, mpacket_size]
			bhs[eop#]
			alu[@buffer_handle, --, b, buffer_handle]
			immed[mpacket_size, 64]
			alu[sop_eop, sop_eop, and, 2]
		eop#:

			alu[@packet_offset, @packet_offset, +, mpacket_size]
		.end

		/*
		 * Wait until there's enough space in the TBUF.
		 */
		.begin
			.reg read $tx
			.reg temp
			.sig zzz

			ctx_arb[sig2]

			br[test_space#]

		loop_space#:
			msf[read, $tx, zero, TX_SEQUENCE_0, 1], ctx_swap[zzz]

			alu[temp, $tx, -, @old_tx_seq_0]
			alu[temp, temp, and, 0xff]
			alu[@mpkts_in_flight, @mpkts_in_flight, -, temp]

			alu[@old_tx_seq_0, --, b, $tx]

		test_space#:
			alu[--, PARTITION_THRESH, -, @mpkts_in_flight]
			blo[loop_space#]

			alu[@mpkts_in_flight, @mpkts_in_flight, +, 1]

			local_csr_wr[SAME_ME_SIGNAL, (0x80 | (&sig2 << 3))]
		.end

		/*
		 * Copy the packet data to the TBUF.
		 */
		.begin
			.reg temp
			.sig copy_sig

			alu[temp, mpacket_size, -, 1]
			alu_shf[temp, 0x10, or, temp, >>3]
			alu_shf[temp, 0x10, or, temp, <<21]
			alu_shf[temp, temp, or, tbuf_element_index, <<11]
			alu_shf[--, temp, or, 1, <<18]

			dram[tbuf_wr, --, packet_data, 0, max_8],
					indirect_ref, sig_done[copy_sig]
			ctx_arb[copy_sig]
		.end

		/*
		 * Mark TBUF element as ready-to-be-transmitted.
		 */
		.begin
			.reg write $tsw $tsw2
			.xfer_order $tsw $tsw2
			.reg temp
			.sig zzz

			alu_shf[temp, channel, or, mpacket_size, <<24]
			alu_shf[$tsw, temp, or, sop_eop, <<8]
			immed[$tsw2, 0]

			immed[temp, TBUF_CTRL]
			alu_shf[temp, temp, or, tbuf_element_index, <<3]
			msf[write, $tsw, temp, 0, 2], ctx_swap[zzz]
		.end

		/*
		 * Resynchronise.
		 */
		.begin
			ctx_arb[sig3]
			local_csr_wr[SAME_ME_SIGNAL, (0x80 | (&sig3 << 3))]
		.end

		/*
		 * If this was an EOP mpacket, recycle the TX buffer
	 	 * and signal the host.
		 */
		.begin
			.reg write $stemp
			.sig zzz

			alu[--, sop_eop, and, 1]
			beq[mpacket_tx_loop#]

		tx_done_ring_full#:
			br_inp_state[SCR_Ring3_Status, tx_done_ring_full#]

			alu[$stemp, --, b, buffer_handle]
			scratch[put, $stemp, zero, 12, 1], ctx_swap[zzz]
			cap[fast_wr, 0, XSCALE_INT_A]
			br[mpacket_tx_loop#]
		.end
	.end


zero_byte_packet#:
	halt


