#!/usr/bin/perl

### ToDo
# Properly implement -columns in the "my %lists" definition...
#
# .Xr requires at least 1 arg, the code here expects at least 2
###

package mdoc2man;
use strict;
use warnings;
use File::Basename;
use lib dirname(__FILE__);
use Mdoc qw(hs ns pp mapwords son soff stoggle gen_encloser);

########
## Basic
########

Mdoc::def_macro( '.Sh', sub { '.SH', hs, @_ }, raw => 1);
Mdoc::def_macro( '.Ss', sub { '.SS', hs, @_ }, raw => 1);
Mdoc::def_macro( '.Pp', sub { ".sp \\n(Ppu\n.ne 2\n" } );
Mdoc::def_macro( '.Nd', sub { "\\- @_" } );

# Macros that enclose things
Mdoc::def_macro( '.Brq', gen_encloser(qw({ }))          , greedy => 1 );
Mdoc::def_macro( '.Op' , gen_encloser(qw([ ]))          , greedy => 1 );
Mdoc::def_macro( '.Qq' , gen_encloser(qw(" "))          , greedy => 1 );
Mdoc::def_macro( '.Dq' , gen_encloser(qw(\*[Lq] \*[Rq])), greedy => 1 );
Mdoc::def_macro( '.Ql' , gen_encloser(qw(\[oq] \[cq]))  , greedy => 1 );
Mdoc::def_macro( '.Sq' , gen_encloser(qw(\[oq] \[cq]))  , greedy => 1 );
Mdoc::def_macro( '.Pq' , gen_encloser(qw/( )/)          , greedy => 1 );
Mdoc::def_macro( '.D1' , sub { ".in +4\n", ns, @_ , ns , "\n.in -4" } , greedy => 1);

Mdoc::def_macro( 'Oo',  sub { '[', @_ } );
Mdoc::def_macro( 'Oc',  sub { ']', @_ } );

Mdoc::def_macro( 'Po',  sub { '(', @_} );
Mdoc::def_macro( 'Pc',  sub { ')', @_ } );

Mdoc::def_macro( 'Bro', sub { '{', ns, @_ } );
Mdoc::def_macro( 'Brc', sub { '}', @_ } );

Mdoc::def_macro( '.Oo',  gen_encloser(qw([ ])), concat_until => '.Oc' );
Mdoc::def_macro( '.Bro', gen_encloser(qw({ })), concat_until => '.Brc' );
Mdoc::def_macro( '.Po',  gen_encloser(qw/( )/), concat_until => '.Pc' );

Mdoc::def_macro( '.Ev', sub { @_ } );
Mdoc::def_macro( '.An', sub { ".NOP ", @_, "\n.br" }, raw => 1 );
Mdoc::def_macro( '.Li', sub { mapwords {"\\f[C]$_\\f[]"} @_ } );
Mdoc::def_macro( '.Cm', sub { mapwords {"\\f\\*[B-Font]$_\\f[]"} @_ } );
Mdoc::def_macro( '.Ic', sub { mapwords {"\\f\\*[B-Font]$_\\f[]"} @_ } );
Mdoc::def_macro( '.Fl', sub { mapwords {"\\f\\*[B-Font]\\-$_\\f[]"} @_ } );
Mdoc::def_macro( '.Ar', sub { mapwords {"\\f\\*[I-Font]$_\\f[]"} @_ } );
Mdoc::def_macro( '.Em', sub { mapwords {"\\fI$_\\f[]"} @_ } );
Mdoc::def_macro( '.Va', sub { mapwords {"\\fI$_\\f[]"} @_ } );
Mdoc::def_macro( '.Sx', sub { mapwords {"\\fI$_\\f[]"} @_ } );
Mdoc::def_macro( '.Xr', sub { "\\fC".(shift)."\\fR(".(shift).")\\f[]", @_ } );
Mdoc::def_macro( '.Fn', sub { "\\f\\*[B-Font]".(shift)."\\fR()\\f[]" } );
Mdoc::def_macro( '.Fn', sub { "\\fB".(shift)."\\fR()\\f[]" } );
Mdoc::def_macro( '.Fx', sub { "FreeBSD", @_ } );
Mdoc::def_macro( '.Ux', sub { "UNIX", @_ } );

Mdoc::def_macro( '.No', sub { ".NOP", map { ($_, ns) } @_ } );
Mdoc::def_macro( '.Pa', sub { mapwords {"\\fI$_\\f[]"} @_; } );
{
    my $name;
    Mdoc::def_macro('.Nm', sub {
        $name = shift if (!$name);
        "\\f\\*[B-Font]$name\\fP", @_
    } );
}

########
## lists
########

my %lists = (
    bullet => sub {
        Mdoc::def_macro('.It', sub { '.IP \fB\(bu\fP 2' });
    },

    column => sub {
        Mdoc::def_macro('.It', sub { '.IP \fB\(bu\fP 2' });
    },

    tag    => sub {
        my (%opts) = @_;

        my $width = '';

        if (exists $opts{width}) {
            $width = ' '.((length $opts{width})+1);
        }

        if (exists $opts{compact}) {
            my $dobrns = 0;
            Mdoc::def_macro('.It', sub {
                    my @ret = (".TP$width\n.NOP", hs);
                    if ($dobrns) {
                        ".br\n.ns\n", ns, @ret, @_;
                    }
                    else {
                        $dobrns = 1;
                        @ret, @_;
                    }
                }, raw => 1);
        }
        else {
            Mdoc::def_macro('.It', sub {
                    ".TP$width\n.NOP", hs, @_
                }, raw => 1);
        }
    },
);

Mdoc::set_Bl_callback(do { my $nested = 0; sub {
    my $type = shift;
    my %opts = Mdoc::parse_opts(@_);
    if (defined $type && $type =~ /-(\w+)/ && exists $lists{$1}) {

        # Wrap nested lists with .RS and .RE
        Mdoc::set_El_callback(sub { 
                return '.RE' if $nested-- > 1;
                return '.PP';
            });

        $lists{$1}->(%opts);

        if ($nested++) {
            return ".RS";
        }
        else {
            return ();
        }
    }
    else {
        die "Invalid list type <$type>";
    }
}}, raw => 1);

# don't bother with arguments for now and do what mdoc2man'.sh' did

Mdoc::def_macro('.Bd', sub { ".br\n.in +4\n.nf" } );
Mdoc::def_macro('.Ed', sub { ".in -4\n.fi" } );

Mdoc::set_Re_callback(sub { 
        my ($reference) = @_;
        <<"REF";
$reference->{authors},
\\fI$reference->{title}\\fR,
$reference->{optional}\n.PP
REF
});

# Define all macros which have the same sub for inline and standalone macro
for (qw(Xr Em Ar Fl Ic Cm Qq Op Nm Pa Sq Li Va Brq Pq Fx Ux)) {
    my $m = Mdoc::get_macro(".$_");
    Mdoc::def_macro($_, delete $m->{run}, %$m);
}

sub print_line {
    print shift;
    print "\n";
}

sub run {
    print <<'DEFS';
.de1 NOP
.  it 1 an-trap
.  if \\n[.$] \,\\$*\/
..
.ie t \
.ds B-Font [CB]
.ds I-Font [CI]
.ds R-Font [CR]
.el \
.ds B-Font B
.ds I-Font I
.ds R-Font R
DEFS

    while (my ($macro, @args) = Mdoc::parse_line(\*STDIN, \&print_line)) {
        my @ret = Mdoc::call_macro($macro, @args);
        print_line(Mdoc::to_string(@ret)) if @ret;
    }
    return 0;
}

exit run(@ARGV) unless caller;

1;
__END__
