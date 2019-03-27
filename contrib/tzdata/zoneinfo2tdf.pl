#! /usr/bin/perl -w
# Summarize .zi input in a .zi-like format.

# Courtesy Ken Pizzini.

use strict;

#This file released to the public domain.

# Note: error checking is poor; trust the output only if the input
# has been checked by zic.

my $contZone = '';
while (<>) {
  my $origline = $_;
  my @fields = ();
  while (s/^\s*((?:"[^"]*"|[^\s#])+)//) {
    push @fields, $1;
  }
  next unless @fields;

  my $type = lc($fields[0]);
  if ($contZone) {
    @fields >= 3 or warn "bad continuation line";
    unshift @fields, '+', $contZone;
    $type = 'zone';
  }

  $contZone = '';
  if ($type eq 'zone') {
    # Zone  NAME  GMTOFF  RULES/SAVE  FORMAT  [UNTIL]
    my $nfields = @fields;
    $nfields >= 5 or warn "bad zone line";
    if ($nfields > 6) {
      #this splice is optional, depending on one's preference
      #(one big date-time field, or componentized date and time):
      splice(@fields, 5, $nfields-5, "@fields[5..$nfields-1]");
    }
    $contZone = $fields[1] if @fields > 5;
  } elsif ($type eq 'rule') {
    # Rule  NAME  FROM  TO  TYPE  IN  ON  AT  SAVE  LETTER/S
    @fields == 10 or warn "bad rule line";
  } elsif ($type eq 'link') {
    # Link  TARGET  LINK-NAME
    @fields == 3 or warn "bad link line";
  } elsif ($type eq 'leap') {
    # Leap  YEAR  MONTH  DAY  HH:MM:SS  CORR  R/S
    @fields == 7 or warn "bad leap line";
  } else {
    warn "Fubar at input line $.: $origline";
  }
  print join("\t", @fields), "\n";
}
