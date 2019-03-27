#!/usr/bin/env perl
# $Id: demo.pl,v 1.23 2018/06/12 21:39:44 tom Exp $
################################################################################
#  Copyright 2018	Thomas E. Dickey
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU Lesser General Public License, version 2.1
#  as published by the Free Software Foundation.
#
#  This program is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public
#  License along with this program; if not, write to
#	Free Software Foundation, Inc.
#	51 Franklin St., Fifth Floor
#	Boston, MA 02110, USA.
################################################################################
# This demonstration is provided solely to exercise the sample Perl wrapper for
# dialog which is included in its source-code.  See libui-dialog-perl for a
# more comprehensive binding.
#
# TODO: modify dialog.pl to use $DIALOG environment variable, drive from GetOpts here
# TODO: eliminate constant $scr_lines in dialog.pl

use warnings;
use strict;
use diagnostics;

use FindBin qw($Bin $Script);
use lib "$Bin";

require "dialog.pl";
dialog->import('@dialog_result');
our @dialog_result;

sub tput($$) {
    my $name    = shift;
    my $default = shift;
    my $value   = `tput "$name"`;
    chomp $value;
    $value = $default unless ( $value =~ /^[0-9]+$/ );
    return $value;
}

sub napms($) {
    my $msecs = shift;
    select( undef, undef, undef, $msecs * 0.001 );
}

sub show_results($$) {
    my $title = shift;
    my $width = shift;
    &rhs_msgbox(
        $title,
        sprintf(
            "Resulting text:\\n    %s", join( '\\n    ', @dialog_result )
        ),
        $width
    );
}

sub doit() {
    my $status         = 1;
    my $RHS_CLEAR      = "clear";
    my $RHS_TEXTBOX    = "textbox";
    my $RHS_MSGBOX     = "msgbox";
    my $RHS_INFOBOX    = "infobox";
    my $RHS_YESNO      = "yesno";
    my $RHS_GAUGE      = "gauge";
    my $RHS_INPUTBOX   = "inputbox";
    my $RHS_MENU       = "menu";
    my $RHS_MENUL      = "menul";
    my $RHS_MENUA      = "menua";
    my $RHS_CHECKLIST  = "checklist";
    my $RHS_CHECKLISTL = "checklistl";
    my $RHS_CHECKLISTA = "checklista";
    my $RHS_RADIOLIST  = "radiolist";

    my @demo_2col = qw(
      This      that
      is        has
      a         this
      2-column  quoted
      menu      "tag".
    );
    my @demo_3col;
    my @demo_tags;
    my %demo_hash;

    for ( my $s = 0, my $t = 0 ; $s <= $#demo_2col ; $s += 2, $t += 3 ) {
        my $d  = $s / 2;
        my $c1 = $demo_2col[$s];
        my $c2 = $demo_2col[ $s + 1 ];
        $demo_3col[$t] = $c1;
        $demo_3col[ $t + 1 ] = $c2;
        $demo_3col[ $t + 2 ] = ( $c1 =~ /2/ ) ? 1 : 0;
        $demo_tags[$d] = $c1;
        $demo_tags[$d] =~ s/2/1/;
        $demo_tags[ $d + ( $#demo_2col + 1 ) / 2 ] = $c2;
        $demo_hash{ sprintf( "%d %s", $d, $c1 ) } = $c2;
    }

    while ( $status > 0 ) {
        my $lines = &tput( "lines", 24 );
        my $cols  = &tput( "cols",  80 );
        my $maxcols = $cols - 4;
        my $mincols = ( $cols > 8 ) ? 8 : $cols;
        my $midcols = int( ( $cols * 3 ) / 4 );

        @dialog_result = ();
        $status        = &rhs_menu(
            "My title",      "My message",
            0,               14,
            $RHS_CLEAR,      "clear and exit",
            $RHS_TEXTBOX,    "text-box of this script",
            $RHS_MSGBOX,     "informational-message, OK button",
            $RHS_INFOBOX,    "informational-message, no button",
            $RHS_YESNO,      "message with Yes/No buttons",
            $RHS_GAUGE,      "message with progress-gauge",
            $RHS_INPUTBOX,   "input-box",
            $RHS_MENU,       "menu, with tags and description",
            $RHS_MENUL,      "menu, using only tags",
            $RHS_MENUA,      "alphabetically sorted menu",
            $RHS_CHECKLIST,  "check-list with tags and description",
            $RHS_CHECKLISTL, "check-list using only tags",
            $RHS_CHECKLISTA, "alphabetically sorted check-list",
            $RHS_RADIOLIST,  "list of radio-buttons"
        );
        if ( $status > 0 and $#dialog_result == 0 ) {

            my $testcase = $dialog_result[0];
            if ( $testcase eq $RHS_CLEAR ) {
                &rhs_clear;
                last;
            }
            elsif ( $testcase eq $RHS_TEXTBOX ) {
                &rhs_textbox( "This script", "$Script", 0, 0 );
            }
            elsif ( $testcase eq $RHS_MSGBOX ) {
                my $msg =
                    "This is a demonstration script.\\n"
                  . "This should be the second line,\\n"
                  . "and this should be the third line,";
                &rhs_msgbox( "A message", $msg,
                    int( ( length($msg) + 3 ) / 3 ) + 3 );
            }
            elsif ( $testcase eq $RHS_INFOBOX ) {
                my $msg =
                    "This is a fairly long line of text, used to"
                  . " show how dialog can be used to wrap lines to fit in"
                  . " screens with different width.  The text will start wide,"
                  . " then get narrower, showing a new infobox for each width"
                  . " before going back up to the full width of the terminal.";
                my $wide = $maxcols;
                while ( $wide > $mincols ) {
                    &rhs_infobox( "Info-box", $msg, $wide-- );
                    &napms(50);
                }
                while ( $wide < $maxcols ) {
                    &rhs_infobox( "Info-box", $msg, ++$wide );
                    &napms(50);
                }
                &rhs_msgbox( "Info-end", $msg, $wide );
            }
            elsif ( $testcase eq $RHS_YESNO ) {
                if (
                    &rhs_yesno(
                        "Yes/no",
                        "Should \"dialog --yesno\" return \"1\" on \"yes\""
                          . " to simplify (some) shell scripts?",
                        $cols / 2
                    )
                  )
                {
                    &rhs_msgbox(
                        "Explanation",
                        "No, a successful program exits with "
                          . "\"0\" (EXIT_SUCCESS)",
                        $cols / 2
                    );
                }
                else {
                    &rhs_msgbox(
                        "Explanation",
                        "Shell scripts assume that \"exit\ 0\" is successful;"
                          . " Perl is different.",
                        $cols / 2
                    );
                }
            }
            elsif ( $testcase eq $RHS_GAUGE ) {
                my $pct = 0;
                my $sec = 10;
                &rhs_gauge(
                    "My gauge",
                    "Show progress (or lack of it)",
                    $midcols * 3, $pct
                );
                while ( $pct < 100 ) {
                    $pct++;
                    &napms($sec);
                    $sec *= 1.04;
                    &rhs_update_gauge($pct);
                }
                $pct = 99;
                &rhs_update_gauge_and_message( "This will go faster", $pct );
                while ( $pct > 0 ) {
                    $pct--;
                    &napms($sec);
                    $sec /= 1.05;
                    &rhs_update_gauge($pct);
                }
                &napms(1000);
                &rhs_stop_gauge;
            }
            elsif ( $testcase eq $RHS_INPUTBOX ) {
                if (
                    &rhs_inputbox(
                        "My inputbox", "This demonstrates the inputbox",
                        $maxcols,      ""
                    )
                  )
                {
                    &show_results( "My inputbox", $midcols );
                }
            }
            elsif ( $testcase eq $RHS_MENU ) {
                if (
                    &rhs_menu(
                        (
                            "A menu",
                            "This menu uses \"tag\" values and descriptions:",
                            $midcols, ( $#demo_2col + 1 ) / 2
                        ),
                        @demo_2col
                    )
                  )
                {
                    &show_results( "My menu", $midcols );
                }
            }
            elsif ( $testcase eq $RHS_MENUL ) {
                if (
                    &rhs_menul(
                        (
                            "A menu", "This menu uses only the \"tag\" values:",
                            $midcols, $#demo_tags + 1
                        ),
                        @demo_tags
                    )
                  )
                {
                    &show_results( "My long-menu", $midcols );
                }
            }
            elsif ( $testcase eq $RHS_MENUA ) {
                if (
                    &rhs_menua(
                        "A menu", "This menu uses the sorted keys from a hash:",
                        $midcols, %demo_hash
                    )
                  )
                {
                    &show_results( "My alpha-menu", $midcols );
                }
            }
            elsif ( $testcase eq $RHS_CHECKLIST ) {
                if (
                    &rhs_checklist(
                        (
                            "A checklist",
                            "This checklist uses \"tag\" values"
                              . " and descriptions:",
                            $midcols,
                            ( $#demo_3col + 1 ) / 3
                        ),
                        @demo_3col
                    )
                  )
                {
                    &show_results( "My checklist", $midcols );
                }
            }
            elsif ( $testcase eq $RHS_CHECKLISTL ) {
                if (
                    &rhs_checklistl(
                        (
                            "A checklist",
                            "This checklist uses only the \"tag\" values:",
                            $midcols, $#demo_tags + 1
                        ),
                        @demo_tags
                    )
                  )
                {
                    &show_results( "My long-checklist", $midcols );
                }
            }
            elsif ( $testcase eq $RHS_CHECKLISTA ) {
                if (
                    &rhs_checklista(
                        "A checklist",
                        "This checklist uses the sorted keys from a hash:",
                        $midcols, %demo_hash
                    )
                  )
                {
                    &show_results( "My alpha-checklist", $midcols );
                }
            }
            elsif ( $testcase eq $RHS_RADIOLIST ) {
                if (
                    &rhs_radiolist(
                        (
                            "A radiolist",
                            "This radiolist uses \"tag\" values"
                              . " and descriptions:",
                            $midcols,
                            ( $#demo_3col + 1 ) / 3
                        ),
                        @demo_3col
                    )
                  )
                {
                    &show_results( "My radiolist", $midcols );
                }
            }
        }
    }
}

&doit;

1;
