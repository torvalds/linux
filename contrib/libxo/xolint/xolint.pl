#!/usr/bin/env perl
#
# Copyright (c) 2014, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.
# Phil Shafer, August 2014
#
#
# xolint -- a lint for inspecting xo_emit format strings
#
# Yes, that's a long way to go for a pun.

%vocabulary = ();

sub main {
    while ($ARGV[0] =~ /^-/) {
	$_ = shift @ARGV;
	$opt_cpp = 1 if /^-c/;
	$opt_cflags .= shift @ARGV if /^-C/;
	$opt_debug = 1 if /^-d/;
	extract_docs() if /^-D/;
	$opt_info = $opt_vocabulary = 1 if /^-I/;
	$opt_print = 1 if /^-p/;
	$opt_vocabulary = 1 if /^-V/;
	extract_samples() if /^-X/;
    }

    if ($#ARGV < 0) {
	print STDERR "xolint [options] files ...\n";
	print STDERR "    -c    invoke 'cpp' on input\n";
	print STDERR "    -C flags   Pass flags to cpp\n";
	print STDERR "    -d         Show debug output\n";
	print STDERR "    -D         Extract xolint documentation\n";
	print STDERR "    -I         Print xo_info_t data\n";
	print STDERR "    -p         Print input data on errors\n";
	print STDERR "    -V         Print vocabulary (list of tags)\n";
	print STDERR "    -X         Print examples of invalid use\n";
	exit(1);
    }

    for $file (@ARGV) {
	parse_file($file);
    }

    if ($opt_info) {
	print "static xo_info_t xo_info_table[] = {\n";
	for $name (sort(keys(%vocabulary))) {
	    print "    { \"", $name, "\", \"type\", \"desc\" },\n";
	}
	print "};\n";
	print "static int xo_info_count = "
	    . "(sizeof(xo_info_table) / sizeof(xo_info_table[0]));\n\n";
	print "#define XO_SET_INFO() \\\n";
	print "    xo_set_info(NULL, xo_info_table, xo_info_count)\n";
    } elsif ($opt_vocabulary) {
	for $name (sort(keys(%vocabulary))) {
	    print $name, "\n";
	}
    }
}

sub extract_samples {
    my $x = "\#" . "\@";
    my $cmd = "grep -B1 -i '$x Should be' $0 | grep xo_emit | sed 's/.*\#*\@//'";
    system($cmd);
    exit(0);
}

sub extract_docs {
    my $x = "\#" . "\@";
    my $cmd = "grep -B1 '$x' $0";
    open INPUT, "$cmd |";
    local @input = <INPUT>;
    close INPUT;
    my $ln, $new = 0, $first = 1, $need_nl;

    for ($ln = 0; $ln <= $#input; $ln++) {
	chomp($_ = $input[$ln]);
	if (/^--/) {
	    $ln += 1;
	    $new = 1;
	    next;
	}
	if ($first) {
	    $new = 1;
	    $first = 0;
	    next;
	}

	s/\s*\#\@\s*//;

	if ($new) {
	    if ($need_nl) {
		print "\n\n";
		$need_nl = 0;
	    }

	    print "*** '$_'\n\n";
	    print "The message \"$_\" can be caused by code like:\n\n";
	    $new = 0;

	} elsif (/xo_emit\s*\(/) {
	    s/^\s+//;
	    print "    $_\n\n";

	} elsif (/^Should be/i) {
	    print "This code should be replaced with code like:\n\n";

	} else {
	    print "$_\n";
	    $need_nl = 1;
	}
    }

    exit(0);
}

sub parse_file {
    local($file) = @_;
    local($errors, $warnings, $info) = (0, 0, 0);
    local $curfile = $file;
    local $curln = 0;

    if ($opt_cpp) {
	die "no such file" unless -f $file;
	open INPUT, "cpp $opt_cflags $file |";
    } else {
	open INPUT, $file || die "cannot open input file '$file'";
    }
    local @input = <INPUT>;
    close INPUT;

    local $ln, $rln, $line, $replay;

    for ($ln = 0; $ln < $#input; $ln++) {
	$line = $input[$ln];
	$curln += 1;

	if ($line =~ /^\#/) {
	    my($num, $fn) = ($line =~ /\#\s*(\d+)\s+"(.+)"/);
	    ($curfile, $curln) = ($fn, $num) if $num;
	    next;
	}

	next unless $line =~ /xo_emit\(/;

	@tokens = parse_tokens();
	print "token:\n    '" . join("'\n    '", @tokens) . "'\n"
	    if $opt_debug;
	check_format($tokens[0]);
    }

    print $file . ": $errors errors, $warnings warnings, $info info\n"
	unless $opt_vocabulary;
}

sub parse_tokens {
    my $full = "$'";
    my @tokens = ();
    my %pairs = ( "{" => "}", "[" => "]", "(" => ")" );
    my %quotes = ( "\"" => "\"", "'" => "'" );
    local @data = split(//, $full);
    local @open = ();
    local $current = "";
    my $quote = "";
    local $off = 0;
    my $ch;

    $replay = $curln . "     " . $line;
    $rln = $ln + 1;

    for (;;) {
	get_tokens() if $off > $#data;
	die "out of data" if $off > $#data;
	$ch = $data[$off++];

	print "'$ch' ($quote) ($#open) [" . join("", @open) . "]\n"
	    if $opt_debug;

	last if $ch eq ";" && $#open < 0;

	if ($ch eq "," && $quote eq "" && $#open < 0) {
	    print "[$current]\n" if $opt_debug;
	    push @tokens, $current;
	    $current = "";
	    next;
	}

	next if $ch =~ /[ \t\n\r]/ && $quote eq "" && $#open < 0;

	$current .= $ch;

	if ($quote) {
	    if ($ch eq $quote) {
		$quote = "";
	    }
	    next;
	}
	if ($quotes{$ch}) {
	    $quote = $quotes{$ch};
	    $current = substr($current, 0, -2) if $current =~ /""$/;
	    next;
	}

	if ($pairs{$ch}) {
	    push @open, $pairs{$ch};
	    next;
	}

	if ($#open >= 0 && $ch eq $open[$#open]) {
	    pop @open;
	    next;
	}
    }

    push @tokens, substr($current, 0, -1);
    return @tokens;
}

sub get_tokens {
    if ($ln + 1 < $#input) {
	$line = $input[++$ln];
	$curln += 1;
	$replay .= $curln . "     " . $line;
	@data = split(//, $line);
	$off = 0;
    }
}

sub check_format {
    my($format) = @_;

    return unless $format =~ /^".*"$/;

    my @data = split(//, $format);
    my $ch;
    my $braces = 0;
    local $count = 0;
    my $content = "";
    my $off;
    my $phase = 0;
    my @build = ();
    local $last, $prev = "";

    # Nukes quotes
    pop @data;
    shift @data;

    for (;;) {
	last if $off > $#data;
	$ch = $data[$off++];

	if ($ch eq "\\") {
	    $ch = $data[$off++];
	    $off += 1 if $ch eq "\\"; # double backslash: "\\/"
	    next;
	}

	if ($braces) {
	    if ($ch eq "}") {
		check_field(@build);
		$braces = 0;
		@build = ();
		$phase = 0;
		next;
	    } elsif ($phase == 0 && $ch eq ":") {
		$phase += 1;
		next;
	    } elsif ($ch eq "/") {
		$phase += 1;
		next;
	    }

	} else {
	    if ($ch eq "{") {
		check_text($build[0]) if length($build[0]);
		$braces = 1;
		@build = ();
		$last = $prev;
		next;
	    }
	    $prev = $ch;
	}

	$build[$phase] .= $ch;
    }

    if ($braces) {
	error("missing closing brace");
	check_field(@build);
    } else {
	check_text($build[0]) if length($build[0]);
    }
}

sub check_text {
    my($text) = @_;

    print "checking text: [$text]\n" if $opt_debug;

    #@ A percent sign appearing in text is a literal
    #@     xo_emit("cost: %d", cost);
    #@ Should be:
    #@     xo_emit("{L:cost}: {:cost/%d}", cost);
    #@ This can be a bit surprising and could be a field that was not
    #@ properly converted to a libxo-style format string.
    info("a percent sign appearing in text is a literal") if $text =~ /%/;
}

%short = (
    # Roles
    "color" => "C",
    "decoration" => "D",
    "error" => "E",
    "label" => "L",
    "note" => "N",
    "padding" => "P",
    "title" => "T",
    "units" => "U",
    "value" => "V",
    "warning" => "W",
    "start-anchor" => "[",
    "stop-anchor" => "]",
    # Modifiers
    "colon" => "c",
    "display" => "d",
    "encoding" => "e",
    "hn" => "h",
    "hn-decimal" => "@",
    "hn-space" => "@",
    "hn-1000" => "@",
    "humanize" => "h",
    "key" => "k",
    "leaf-list" => "l",
    "no-quotes" => "n",
    "quotes" => "q",
    "trim" => "t",
    "white" => "w",
 );

sub check_field {
    my(@field) = @_;
    print "checking field: [" . join("][", @field) . "]\n" if $opt_debug;

    if ($field[0] =~ /,/) {
	# We have long names; deal with it by turning them into short names
	my @parts = split(/,/, $field[0]);
	my $new = "";
	for (my $i = 1; $i <= $#parts; $i++) {
	    my $v = $parts[$i];
	    $v =~ s/^\s+//;
	    $v =~ s/\s+$//;
	    if ($short{$v} eq "@") {
		# ignore; has no short version
	    } elsif ($short{$v}) {
		$new .= $short{$v};
	    } else {
		#@ Unknown long name for role/modifier
		#@   xo_emit("{,humanization:value}", value);
		#@ Should be:
		#@   xo_emit("{,humanize:value}", value);
		#@ The hn-* modifiers (hn-decimal, hn-space, hn-1000)
		#@ are only valid for fields with the {h:} modifier.
		error("Unknown long name for role/modifier ($v)");
	    }
	}

	$field[4] = substr($field[0], index($field[0], ","));
	$field[0] = $parts[0] . $new;
    }

    if ($opt_vocabulary) {
	$vocabulary{$field[1]} = 1
	    if $field[1] && $field[0] !~ /[DELNPTUW\[\]]/;
	return;
    }

    #@ Last character before field definition is a field type
    #@ A common typo:
    #@     xo_emit("{T:Min} T{:Max}");
    #@ Should be:
    #@     xo_emit("{T:Min} {T:Max}");
    #@ Twiddling the "{" and the field role is a common typo.
    info("last character before field definition is a field type ($last)")
	if $last =~ /[DELNPTUVW\[\]]/ && $field[0] !~ /[DELNPTUVW\[\]]/;

    #@ Encoding format uses different number of arguments
    #@     xo_emit("{:name/%6.6s %%04d/%s}", name, number);
    #@ Should be:
    #@     xo_emit("{:name/%6.6s %04d/%s-%d}", name, number);
    #@ Both format should consume the same number of arguments off the stack
    my $cf = count_args($field[2]);
    my $ce = count_args($field[3]);
    warn("encoding format uses different number of arguments ($cf/$ce)")
	if $ce >= 0 && $cf >= 0 && $ce != $cf;

    #@ Only one field role can be used
    #@     xo_emit("{LT:Max}");
    #@ Should be:
    #@     xo_emit("{T:Max}");
    my(@roles) = ($field[0] !~ /([DELNPTUVW\[\]]).*([DELNPTUVW\[\]])/);
    error("only one field role can be used (" . join(", ", @roles) . ")")
	if $#roles > 0;

    # Field is a color, note, label, or title
    if ($field[0] =~ /[CDLNT]/) {

	#@ Potential missing slash after C, D, N, L, or T with format
	#@     xo_emit("{T:%6.6s}\n", "Max");
	#@ should be:
	#@     xo_emit("{T:/%6.6s}\n", "Max");
	#@ The "%6.6s" will be a literal, not a field format.  While
	#@ it's possibly valid, it's likely a missing "/".
	info("potential missing slash after C, D, N, L, or T with format")
	    if $field[1] =~ /%/;

	#@ An encoding format cannot be given (roles: DNLT)
	#@    xo_emit("{T:Max//%s}", "Max");
	#@ Fields with the C, D, N, L, and T roles are not emitted in
	#@ the 'encoding' style (JSON, XML), so an encoding format
	#@ would make no sense.
	error("encoding format cannot be given when content is present")
	    if $field[3];
    }

    # Field is a color, decoration, label, or title
    if ($field[0] =~ /[CDLN]/) {
	#@ Format cannot be given when content is present (roles: CDLN)
	#@    xo_emit("{N:Max/%6.6s}", "Max");
	#@ Fields with the C, D, L, or N roles can't have both
	#@ static literal content ("{L:Label}") and a
	#@ format ("{L:/%s}").
	#@ This error will also occur when the content has a backslash
	#@ in it, like "{N:Type of I/O}"; backslashes should be escaped,
	#@ like "{N:Type of I\\/O}".  Note the double backslash, one for
	#@ handling 'C' strings, and one for libxo.
	error("format cannot be given when content is present")
	    if $field[1] && $field[2];
    }

    # Field is a color/effect
    if ($field[0] =~ /C/) {
	if ($field[1]) {
	    my $val;
	    my @sub = split(/,/, $field[1]);
	    grep { s/^\s*//; s/\s*$//; } @sub;

	    for $val (@sub) {
		if ($val =~ /^(default,black,red,green,yellow,blue,magenta,cyan,white)$/) {

		    #@ Field has color without fg- or bg- (role: C)
		    #@   xo_emit("{C:green}{:foo}{C:}", x);
		    #@ Should be:
		    #@   xo_emit("{C:fg-green}{:foo}{C:}", x);
		    #@ Colors must be prefixed by either "fg-" or "bg-".
		    error("Field has color without fg- or bg- (role: C)");

		} elsif ($val =~ /^(fg|bg)-(default|black|red|green|yellow|blue|magenta|cyan|white)$/) {
		    # color
		} elsif ($val =~ /^(bold|underline)$/) {
		} elsif ($val =~ /^(no-)?(bold|underline|inverse)$/) {
		    # effect

		} elsif ($val =~ /^(reset|normal)$/) {
		    # effect also
		} else {
		    #@ Field has invalid color or effect (role: C)
		    #@   xo_emit("{C:fg-purple,bold}{:foo}{C:gween}", x);
		    #@ Should be:
		    #@   xo_emit("{C:fg-red,bold}{:foo}{C:fg-green}", x);
		    #@ The list of colors and effects are limited.  The
		    #@ set of colors includes default, black, red, green,
		    #@ yellow, blue, magenta, cyan, and white, which must
		    #@ be prefixed by either "fg-" or "bg-".  Effects are
		    #@ limited to bold, no-bold, underline, no-underline,
		    #@ inverse, no-inverse, normal, and reset.  Values must
		    #@ be separated by commas.
		    error("Field has invalid color or effect (role: C) ($val)");
		}
	    }
	}
    }

    # Humanized field
    if ($field[0] =~ /h/) {
	if (length($field[2]) == 0) {
	    #@ Field has humanize modifier but no format string
	    #@   xo_emit("{h:value}", value);
	    #@ Should be:
	    #@   xo_emit("{h:value/%d}", value);
	    #@ Humanization is only value for numbers, which are not
	    #@ likely to use the default format ("%s").
	    error("Field has humanize modifier but no format string");
	}
    }

    # hn-* on non-humanize field
    if ($field[0] !~ /h/) {
	if ($field[4] =~ /,hn-/) {
	    #@ Field has hn-* modifier but not 'h' modifier
	    #@   xo_emit("{,hn-1000:value}", value);
	    #@ Should be:
	    #@   xo_emit("{h,hn-1000:value}", value);
	    #@ The hn-* modifiers (hn-decimal, hn-space, hn-1000)
	    #@ are only valid for fields with the {h:} modifier.
	    error("Field has hn-* modifier but not 'h' modifier");
	}
    }

    # A value field
    if (length($field[0]) == 0 || $field[0] =~ /V/) {

	#@ Value field must have a name (as content)")
	#@     xo_emit("{:/%s}", "value");
	#@ Should be:
	#@     xo_emit("{:tag-name/%s}", "value");
	#@ The field name is used for XML and JSON encodings.  These
	#@ tags names are static and must appear directly in the
	#@ field descriptor.
	error("value field must have a name (as content)")
	    unless $field[1];

	#@ Use hyphens, not underscores, for value field name
	#@     xo_emit("{:no_under_scores}", "bad");
	#@ Should be:
	#@     xo_emit("{:no-under-scores}", "bad");
	#@ Use of hyphens is traditional in XML, and the XOF_UNDERSCORES
	#@ flag can be used to generate underscores in JSON, if desired.
	#@ But the raw field name should use hyphens.
	error("use hyphens, not underscores, for value field name")
	    if $field[1] =~ /_/;

	#@ Value field name cannot start with digit
	#@     xo_emit("{:10-gig/}");
	#@ Should be:
	#@     xo_emit("{:ten-gig/}");
	#@ XML element names cannot start with a digit.
	error("value field name cannot start with digit")
	    if $field[1] =~ /^[0-9]/;

	#@ Value field name should be lower case
	#@     xo_emit("{:WHY-ARE-YOU-SHOUTING}", "NO REASON");
	#@ Should be:
	#@     xo_emit("{:why-are-you-shouting}", "no reason");
	#@ Lower case is more civilized.  Even TLAs should be lower case
	#@ to avoid scenarios where the differences between "XPath" and
	#@ "Xpath" drive your users crazy.  Lower case rules the seas.
	error("value field name should be lower case")
	    if $field[1] =~ /[A-Z]/;

	#@ Value field name should be longer than two characters
	#@     xo_emit("{:x}", "mumble");
	#@ Should be:
	#@     xo_emit("{:something-meaningful}", "mumble");
	#@ Field names should be descriptive, and it's hard to
	#@ be descriptive in less than two characters.  Consider
	#@ your users and try to make something more useful.
	#@ Note that this error often occurs when the field type
	#@ is placed after the colon ("{:T/%20s}"), instead of before
	#@ it ("{T:/20s}").
	error("value field name should be longer than two characters")
	    if $field[1] =~ /[A-Z]/;

	#@ Value field name contains invalid character
	#@     xo_emit("{:cost-in-$$/%u}", 15);
	#@ Should be:
	#@     xo_emit("{:cost-in-dollars/%u}", 15);
	#@ An invalid character is often a sign of a typo, like "{:]}"
	#@ instead of "{]:}".  Field names are restricted to lower-case
	#@ characters, digits, and hyphens.
	error("value field name contains invalid character (" . $field[1] . ")")
	    unless $field[1] =~ /^[0-9a-z-]*$/;
    }

    # A decoration field
    if ($field[0] =~ /D/) {

	#@decoration field contains invalid character
	#@     xo_emit("{D:not good}");
	#@ Should be:
	#@     xo_emit("{D:((}{:good}{D:))}", "yes");
	#@ This is minor, but fields should use proper roles.  Decoration
	#@ fields are meant to hold punctuation and other characters used
	#@ to decorate the content, typically to make it more readable
	#@ to human readers.
	warn("decoration field contains invalid character")
	    unless $field[1] =~ m:^[~!\@\#\$%^&\*\(\);\:\[\]\{\} ]+$:;
    }

    if ($field[0] =~ /[\[\]]/) {
	#@ Anchor content should be decimal width
	#@     xo_emit("{[:mumble}");
	#@ Should be:
	#@     xo_emit("{[:32}");
	#@ Anchors need an integer value to specify the width of
	#@ the set of anchored fields.  The value can be positive
	#@ (for left padding/right justification) or negative (for
	#@ right padding/left justification) and can appear in
	#@ either the start or stop anchor field descriptor.
	error("anchor content should be decimal width")
	    if $field[1] && $field[1] !~ /^-?\d+$/ ;

	#@ Anchor format should be "%d"
	#@     xo_emit("{[:/%s}");
	#@ Should be:
	#@     xo_emit("{[:/%d}");
	#@ Anchors only grok integer values, and if the value is not static,
	#@ if must be in an 'int' argument, represented by the "%d" format.
	#@ Anything else is an error.
	error("anchor format should be \"%d\"")
	    if $field[2] && $field[2] ne "%d";

	#@ Anchor cannot have both format and encoding format")
	#@     xo_emit("{[:32/%d}");
	#@ Should be:
	#@     xo_emit("{[:32}");
	#@ Anchors can have a static value or argument for the width,
	#@ but cannot have both.
	error("anchor cannot have both format and encoding format")
	    if $field[1] && $field[2];
    }
}

sub count_args {
    my($format) = @_;

    return -1 unless $format;

    my $in;
    my($text, $ff, $fc, $rest);
    for ($in = $format; $in; $in = $rest) {
	($text, $ff, $fc, $rest) =
	   ($in =~ /^([^%]*)(%[^%diouxXDOUeEfFgGaAcCsSp]*)([diouxXDOUeEfFgGaAcCsSp])(.*)$/);
	unless ($ff) {
	    # Might be a "%%"
	    ($text, $ff, $rest) = ($in =~ /^([^%]*)(%%)(.*)$/);
	    if ($ff) {
		check_text($text);
	    } else {
		# Not sure what's going on here, but something's wrong...
		error("invalid field format") if $in =~ /%/;
	    }
	    next;
	}

	check_text($text);
	check_field_format($ff, $fc);
    }

    return 0;
}

sub check_field_format {
    my($ff, $fc) = @_;

    print "check_field_format: [$ff] [$fc]\n" if $opt_debug;

    my(@chunks) = split(/\./, $ff);

    #@ Max width only valid for strings
    #@     xo_emit("{:tag/%2.4.6d}", 55);
    #@ Should be:
    #@     xo_emit("{:tag/%2.6d}", 55);
    #@ libxo allows a true 'max width' in addition to the traditional
    #@ printf-style 'max number of bytes to use for input'.  But this
    #@ is supported only for string values, since it makes no sense
    #@ for non-strings.  This error may occur from a typo,
    #@ like "{:tag/%6..6d}" where only one period should be used.
    error("max width only valid for strings")
	if $#chunks >= 2 && $fc !~ /[sS]/;
}

sub error {
    return if $opt_vocabulary;
    print STDERR $curfile . ": " .$curln . ": error: " . join(" ", @_) . "\n";
    print STDERR $replay . "\n" if $opt_print;
    $errors += 1;
}

sub warn {
    return if $opt_vocabulary;
    print STDERR $curfile . ": " .$curln . ": warning: " . join(" ", @_) . "\n";
    print STDERR $replay . "\n" if $opt_print;
    $warnings += 1;
}

sub info {
    return if $opt_vocabulary;
    print STDERR $curfile . ": " .$curln . ": info: " . join(" ", @_) . "\n";
    print STDERR $replay . "\n" if $opt_print;
    $info += 1;
}

main: {
    main();
}
