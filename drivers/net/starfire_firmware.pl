#!/usr/bin/perl

# This script can be used to generate a new starfire_firmware.h
# from GFP_RX.DAT and GFP_TX.DAT, files included with the DDK
# and also with the Novell drivers.

open FW, "GFP_RX.DAT" || die;
open FWH, ">starfire_firmware.h" || die;

printf(FWH "static u32 firmware_rx[] = {\n");
$counter = 0;
while ($foo = <FW>) {
  chomp;
  printf(FWH "  0x%s, 0x0000%s,\n", substr($foo, 4, 8), substr($foo, 0, 4));
  $counter++;
}

close FW;
open FW, "GFP_TX.DAT" || die;

printf(FWH "};\t/* %d Rx instructions */\n#define FIRMWARE_RX_SIZE %d\n\nstatic u32 firmware_tx[] = {\n", $counter, $counter);
$counter = 0;
while ($foo = <FW>) {
  chomp;
  printf(FWH "  0x%s, 0x0000%s,\n", substr($foo, 4, 8), substr($foo, 0, 4));
  $counter++;
}

close FW;
printf(FWH "};\t/* %d Tx instructions */\n#define FIRMWARE_TX_SIZE %d\n", $counter, $counter);
close(FWH);
