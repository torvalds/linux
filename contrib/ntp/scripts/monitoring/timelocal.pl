;# timelocal.pl
;#
;# Usage:
;#	$time = timelocal($sec,$min,$hours,$mday,$mon,$year,$junk,$junk,$isdst);
;#	$time = timegm($sec,$min,$hours,$mday,$mon,$year);

;# These routines are quite efficient and yet are always guaranteed to agree
;# with localtime() and gmtime().  We manage this by caching the start times
;# of any months we've seen before.  If we know the start time of the month,
;# we can always calculate any time within the month.  The start times
;# themselves are guessed by successive approximation starting at the
;# current time, since most dates seen in practice are close to the
;# current date.  Unlike algorithms that do a binary search (calling gmtime
;# once for each bit of the time value, resulting in 32 calls), this algorithm
;# calls it at most 6 times, and usually only once or twice.  If you hit
;# the month cache, of course, it doesn't call it at all.

;# timelocal is implemented using the same cache.  We just assume that we're
;# translating a GMT time, and then fudge it when we're done for the timezone
;# and daylight savings arguments.  The timezone is determined by examining
;# the result of localtime(0) when the package is initialized.  The daylight
;# savings offset is currently assumed to be one hour.

CONFIG: {
    package timelocal;
    
    @epoch = localtime(0);
    $tzmin = $epoch[2] * 60 + $epoch[1];	# minutes east of GMT
    if ($tzmin > 0) {
	$tzmin = 24 * 60 - $tzmin;		# minutes west of GMT
	$tzmin -= 24 * 60 if $epoch[5] == 70;	# account for the date line
    }

    $SEC = 1;
    $MIN = 60 * $SEC;
    $HR = 60 * $MIN;
    $DAYS = 24 * $HR;
    $YearFix = ((gmtime(946684800))[5] == 100) ? 100 : 0;
}

sub timegm {
    package timelocal;

    $ym = pack(C2, @_[5,4]);
    $cheat = $cheat{$ym} || &cheat;
    $cheat + $_[0] * $SEC + $_[1] * $MIN + $_[2] * $HR + ($_[3]-1) * $DAYS;
}

sub timelocal {
    package timelocal;

    $ym = pack(C2, @_[5,4]);
    $cheat = $cheat{$ym} || &cheat;
    $cheat + $_[0] * $SEC + $_[1] * $MIN + $_[2] * $HR + ($_[3]-1) * $DAYS
	+ $tzmin * $MIN - 60 * 60 * ($_[8] != 0);
}

package timelocal;

sub cheat {
    $year = $_[5];
    $month = $_[4];
    $guess = $^T;
    @g = gmtime($guess);
    $year += $YearFix if $year < $epoch[5];
    while ($diff = $year - $g[5]) {
	$guess += $diff * (364 * $DAYS);
	@g = gmtime($guess);
    }
    while ($diff = $month - $g[4]) {
	$guess += $diff * (28 * $DAYS);
	@g = gmtime($guess);
    }
    $g[3]--;
    $guess -= $g[0] * $SEC + $g[1] * $MIN + $g[2] * $HR + $g[3] * $DAYS;
    $cheat{$ym} = $guess;
}
