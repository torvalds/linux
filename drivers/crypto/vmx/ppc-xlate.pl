#!/usr/bin/env perl

# PowerPC assembler distiller by <appro>.

my $flavour = shift;
my $output = shift;
open STDOUT,">$output" || die "can't open $output: $!";

my %GLOBALS;
my $dotinlocallabels=($flavour=~/linux/)?1:0;

################################################################
# directives which need special treatment on different platforms
################################################################
my $globl = sub {
    my $junk = shift;
    my $name = shift;
    my $global = \$GLOBALS{$name};
    my $ret;

    $name =~ s|^[\.\_]||;
 
    SWITCH: for ($flavour) {
	/aix/		&& do { $name = ".$name";
				last;
			      };
	/osx/		&& do { $name = "_$name";
				last;
			      };
	/linux.*(32|64le)/
			&& do {	$ret .= ".globl	$name\n";
				$ret .= ".type	$name,\@function";
				last;
			      };
	/linux.*64/	&& do {	$ret .= ".globl	$name\n";
				$ret .= ".type	$name,\@function\n";
				$ret .= ".section	\".opd\",\"aw\"\n";
				$ret .= ".align	3\n";
				$ret .= "$name:\n";
				$ret .= ".quad	.$name,.TOC.\@tocbase,0\n";
				$ret .= ".previous\n";

				$name = ".$name";
				last;
			      };
    }

    $ret = ".globl	$name" if (!$ret);
    $$global = $name;
    $ret;
};
my $text = sub {
    my $ret = ($flavour =~ /aix/) ? ".csect\t.text[PR],7" : ".text";
    $ret = ".abiversion	2\n".$ret	if ($flavour =~ /linux.*64le/);
    $ret;
};
my $machine = sub {
    my $junk = shift;
    my $arch = shift;
    if ($flavour =~ /osx/)
    {	$arch =~ s/\"//g;
	$arch = ($flavour=~/64/) ? "ppc970-64" : "ppc970" if ($arch eq "any");
    }
    ".machine	$arch";
};
my $size = sub {
    if ($flavour =~ /linux/)
    {	shift;
	my $name = shift; $name =~ s|^[\.\_]||;
	my $ret  = ".size	$name,.-".($flavour=~/64$/?".":"").$name;
	$ret .= "\n.size	.$name,.-.$name" if ($flavour=~/64$/);
	$ret;
    }
    else
    {	"";	}
};
my $asciz = sub {
    shift;
    my $line = join(",",@_);
    if ($line =~ /^"(.*)"$/)
    {	".byte	" . join(",",unpack("C*",$1),0) . "\n.align	2";	}
    else
    {	"";	}
};
my $quad = sub {
    shift;
    my @ret;
    my ($hi,$lo);
    for (@_) {
	if (/^0x([0-9a-f]*?)([0-9a-f]{1,8})$/io)
	{  $hi=$1?"0x$1":"0"; $lo="0x$2";  }
	elsif (/^([0-9]+)$/o)
	{  $hi=$1>>32; $lo=$1&0xffffffff;  } # error-prone with 32-bit perl
	else
	{  $hi=undef; $lo=$_; }

	if (defined($hi))
	{  push(@ret,$flavour=~/le$/o?".long\t$lo,$hi":".long\t$hi,$lo");  }
	else
	{  push(@ret,".quad	$lo");  }
    }
    join("\n",@ret);
};

################################################################
# simplified mnemonics not handled by at least one assembler
################################################################
my $cmplw = sub {
    my $f = shift;
    my $cr = 0; $cr = shift if ($#_>1);
    # Some out-of-date 32-bit GNU assembler just can't handle cmplw...
    ($flavour =~ /linux.*32/) ?
	"	.long	".sprintf "0x%x",31<<26|$cr<<23|$_[0]<<16|$_[1]<<11|64 :
	"	cmplw	".join(',',$cr,@_);
};
my $bdnz = sub {
    my $f = shift;
    my $bo = $f=~/[\+\-]/ ? 16+9 : 16;	# optional "to be taken" hint
    "	bc	$bo,0,".shift;
} if ($flavour!~/linux/);
my $bltlr = sub {
    my $f = shift;
    my $bo = $f=~/\-/ ? 12+2 : 12;	# optional "not to be taken" hint
    ($flavour =~ /linux/) ?		# GNU as doesn't allow most recent hints
	"	.long	".sprintf "0x%x",19<<26|$bo<<21|16<<1 :
	"	bclr	$bo,0";
};
my $bnelr = sub {
    my $f = shift;
    my $bo = $f=~/\-/ ? 4+2 : 4;	# optional "not to be taken" hint
    ($flavour =~ /linux/) ?		# GNU as doesn't allow most recent hints
	"	.long	".sprintf "0x%x",19<<26|$bo<<21|2<<16|16<<1 :
	"	bclr	$bo,2";
};
my $beqlr = sub {
    my $f = shift;
    my $bo = $f=~/-/ ? 12+2 : 12;	# optional "not to be taken" hint
    ($flavour =~ /linux/) ?		# GNU as doesn't allow most recent hints
	"	.long	".sprintf "0x%X",19<<26|$bo<<21|2<<16|16<<1 :
	"	bclr	$bo,2";
};
# GNU assembler can't handle extrdi rA,rS,16,48, or when sum of last two
# arguments is 64, with "operand out of range" error.
my $extrdi = sub {
    my ($f,$ra,$rs,$n,$b) = @_;
    $b = ($b+$n)&63; $n = 64-$n;
    "	rldicl	$ra,$rs,$b,$n";
};
my $vmr = sub {
    my ($f,$vx,$vy) = @_;
    "	vor	$vx,$vy,$vy";
};

# PowerISA 2.06 stuff
sub vsxmem_op {
    my ($f, $vrt, $ra, $rb, $op) = @_;
    "	.long	".sprintf "0x%X",(31<<26)|($vrt<<21)|($ra<<16)|($rb<<11)|($op*2+1);
}
# made-up unaligned memory reference AltiVec/VMX instructions
my $lvx_u	= sub {	vsxmem_op(@_, 844); };	# lxvd2x
my $stvx_u	= sub {	vsxmem_op(@_, 972); };	# stxvd2x
my $lvdx_u	= sub {	vsxmem_op(@_, 588); };	# lxsdx
my $stvdx_u	= sub {	vsxmem_op(@_, 716); };	# stxsdx
my $lvx_4w	= sub { vsxmem_op(@_, 780); };	# lxvw4x
my $stvx_4w	= sub { vsxmem_op(@_, 908); };	# stxvw4x

# PowerISA 2.07 stuff
sub vcrypto_op {
    my ($f, $vrt, $vra, $vrb, $op) = @_;
    "	.long	".sprintf "0x%X",(4<<26)|($vrt<<21)|($vra<<16)|($vrb<<11)|$op;
}
my $vcipher	= sub { vcrypto_op(@_, 1288); };
my $vcipherlast	= sub { vcrypto_op(@_, 1289); };
my $vncipher	= sub { vcrypto_op(@_, 1352); };
my $vncipherlast= sub { vcrypto_op(@_, 1353); };
my $vsbox	= sub { vcrypto_op(@_, 0, 1480); };
my $vshasigmad	= sub { my ($st,$six)=splice(@_,-2); vcrypto_op(@_, $st<<4|$six, 1730); };
my $vshasigmaw	= sub { my ($st,$six)=splice(@_,-2); vcrypto_op(@_, $st<<4|$six, 1666); };
my $vpmsumb	= sub { vcrypto_op(@_, 1032); };
my $vpmsumd	= sub { vcrypto_op(@_, 1224); };
my $vpmsubh	= sub { vcrypto_op(@_, 1096); };
my $vpmsumw	= sub { vcrypto_op(@_, 1160); };
my $vaddudm	= sub { vcrypto_op(@_, 192);  };

my $mtsle	= sub {
    my ($f, $arg) = @_;
    "	.long	".sprintf "0x%X",(31<<26)|($arg<<21)|(147*2);
};

while($line=<>) {

    $line =~ s|[#!;].*$||;	# get rid of asm-style comments...
    $line =~ s|/\*.*\*/||;	# ... and C-style comments...
    $line =~ s|^\s+||;		# ... and skip white spaces in beginning...
    $line =~ s|\s+$||;		# ... and at the end

    {
	$line =~ s|\b\.L(\w+)|L$1|g;	# common denominator for Locallabel
	$line =~ s|\bL(\w+)|\.L$1|g	if ($dotinlocallabels);
    }

    {
	$line =~ s|(^[\.\w]+)\:\s*||;
	my $label = $1;
	if ($label) {
	    printf "%s:",($GLOBALS{$label} or $label);
	    printf "\n.localentry\t$GLOBALS{$label},0"	if ($GLOBALS{$label} && $flavour =~ /linux.*64le/);
	}
    }

    {
	$line =~ s|^\s*(\.?)(\w+)([\.\+\-]?)\s*||;
	my $c = $1; $c = "\t" if ($c eq "");
	my $mnemonic = $2;
	my $f = $3;
	my $opcode = eval("\$$mnemonic");
	$line =~ s/\b(c?[rf]|v|vs)([0-9]+)\b/$2/g if ($c ne "." and $flavour !~ /osx/);
	if (ref($opcode) eq 'CODE') { $line = &$opcode($f,split(',',$line)); }
	elsif ($mnemonic)           { $line = $c.$mnemonic.$f."\t".$line; }
    }

    print $line if ($line);
    print "\n";
}

close STDOUT;
