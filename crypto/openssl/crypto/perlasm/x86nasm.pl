#! /usr/bin/env perl
# Copyright 1999-2018 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


package x86nasm;

*out=\@::out;

$::lbdecor="L\$";		# local label decoration
$nmdecor="_";			# external name decoration
$drdecor=$::mwerks?".":"";	# directive decoration

$initseg="";

sub ::generic
{ my $opcode=shift;
  my $tmp;

    if (!$::mwerks)
    {   if    ($opcode =~ m/^j/o && $#_==0) # optimize jumps
	{   $_[0] = "NEAR $_[0]";   	}
	elsif ($opcode eq "lea" && $#_==1)  # wipe storage qualifier from lea
	{   $_[1] =~ s/^[^\[]*\[/\[/o;	}
	elsif ($opcode eq "clflush" && $#_==0)
	{   $_[0] =~ s/^[^\[]*\[/\[/o;	}
    }
    &::emit($opcode,@_);
  1;
}
#
# opcodes not covered by ::generic above, mostly inconsistent namings...
#
sub ::call	{ &::emit("call",(&::islabel($_[0]) or "$nmdecor$_[0]")); }
sub ::call_ptr	{ &::emit("call",@_);	}
sub ::jmp_ptr	{ &::emit("jmp",@_);	}

sub get_mem
{ my($size,$addr,$reg1,$reg2,$idx)=@_;
  my($post,$ret);

    if (!defined($idx) && 1*$reg2) { $idx=$reg2; $reg2=$reg1; undef $reg1; }

    if ($size ne "")
    {	$ret .= "$size";
	$ret .= " PTR" if ($::mwerks);
	$ret .= " ";
    }
    $ret .= "[";

    $addr =~ s/^\s+//;
    # prepend global references with optional underscore
    $addr =~ s/^([^\+\-0-9][^\+\-]*)/::islabel($1) or "$nmdecor$1"/ige;
    # put address arithmetic expression in parenthesis
    $addr="($addr)" if ($addr =~ /^.+[\-\+].+$/);

    if (($addr ne "") && ($addr ne 0))
    {	if ($addr !~ /^-/)	{ $ret .= "$addr+"; }
	else			{ $post=$addr;      }
    }

    if ($reg2 ne "")
    {	$idx!=0 or $idx=1;
	$ret .= "$reg2*$idx";
	$ret .= "+$reg1" if ($reg1 ne "");
    }
    else
    {	$ret .= "$reg1";   }

    $ret .= "$post]";
    $ret =~ s/\+\]/]/; # in case $addr was the only argument

  $ret;
}
sub ::BP	{ &get_mem("BYTE",@_);  }
sub ::DWP	{ &get_mem("DWORD",@_); }
sub ::WP	{ &get_mem("WORD",@_);	}
sub ::QWP	{ &get_mem("",@_);      }
sub ::BC	{ (($::mwerks)?"":"BYTE ")."@_";  }
sub ::DWC	{ (($::mwerks)?"":"DWORD ")."@_"; }

sub ::file
{   if ($::mwerks)	{ push(@out,".section\t.text,64\n"); }
    else
    { my $tmp=<<___;
%ifidn __OUTPUT_FORMAT__,obj
section	code	use32 class=code align=64
%elifidn __OUTPUT_FORMAT__,win32
\$\@feat.00 equ 1
section	.text	code align=64
%else
section	.text	code
%endif
___
	push(@out,$tmp);
    }
}

sub ::function_begin_B
{ my $func=shift;
  my $global=($func !~ /^_/);
  my $begin="${::lbdecor}_${func}_begin";

    $begin =~ s/^\@/./ if ($::mwerks);	# the torture never stops

    &::LABEL($func,$global?"$begin":"$nmdecor$func");
    $func=$nmdecor.$func;

    push(@out,"${drdecor}global	$func\n")	if ($global);
    push(@out,"${drdecor}align	16\n");
    push(@out,"$func:\n");
    push(@out,"$begin:\n")			if ($global);
    $::stack=4;
}

sub ::function_end_B
{   $::stack=0;
    &::wipe_labels();
}

sub ::file_end
{   if (grep {/\b${nmdecor}OPENSSL_ia32cap_P\b/i} @out)
    {	my $comm=<<___;
${drdecor}segment	.bss
${drdecor}common	${nmdecor}OPENSSL_ia32cap_P 16
___
	# comment out OPENSSL_ia32cap_P declarations
	grep {s/(^extern\s+${nmdecor}OPENSSL_ia32cap_P)/\;$1/} @out;
	push (@out,$comm)
    }
    push (@out,$initseg) if ($initseg);
}

sub ::comment {   foreach (@_) { push(@out,"\t; $_\n"); }   }

sub ::external_label
{   foreach(@_)
    {	push(@out,"${drdecor}extern\t".&::LABEL($_,$nmdecor.$_)."\n");   }
}

sub ::public_label
{   push(@out,"${drdecor}global\t".&::LABEL($_[0],$nmdecor.$_[0])."\n");  }

sub ::data_byte
{   push(@out,(($::mwerks)?".byte\t":"db\t").join(',',@_)."\n");	}
sub ::data_short
{   push(@out,(($::mwerks)?".word\t":"dw\t").join(',',@_)."\n");	}
sub ::data_word
{   push(@out,(($::mwerks)?".long\t":"dd\t").join(',',@_)."\n");	}

sub ::align
{   push(@out,"${drdecor}align\t$_[0]\n");	}

sub ::picmeup
{ my($dst,$sym)=@_;
    &::lea($dst,&::DWP($sym));
}

sub ::initseg
{ my $f=$nmdecor.shift;
    if ($::win32)
    {	$initseg=<<___;
segment	.CRT\$XCU data align=4
extern	$f
dd	$f
___
    }
}

sub ::dataseg
{   if ($mwerks)	{ push(@out,".section\t.data,4\n");   }
    else		{ push(@out,"section\t.data align=4\n"); }
}

sub ::safeseh
{ my $nm=shift;
    push(@out,"%if	__NASM_VERSION_ID__ >= 0x02030000\n");
    push(@out,"safeseh	".&::LABEL($nm,$nmdecor.$nm)."\n");
    push(@out,"%endif\n");
}

1;
