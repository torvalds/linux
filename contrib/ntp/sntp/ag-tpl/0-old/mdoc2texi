#! /usr/bin/perl

### To Do:

# the Bl -column command needs work:
# - support for "-offset" 
# - support for the header widths

# 

###

package mdoc2texi;
use strict;
use warnings;
use File::Basename qw(dirname);
use lib dirname(__FILE__);
use Mdoc qw(ns pp hs mapwords gen_encloser nl);

# Ignore commments
Mdoc::def_macro( '.\"',  sub { () } );

# Enclosers
Mdoc::def_macro( '.An',  sub { @_, ns, '@*' } );
Mdoc::def_macro( '.Aq',  gen_encloser(qw(< >)),   greedy => 1);
Mdoc::def_macro( '.Bq',  gen_encloser(qw([ ])),   greedy => 1);
Mdoc::def_macro( '.Brq', gen_encloser(qw(@{ @})), greedy => 1);
Mdoc::def_macro( '.Pq',  gen_encloser(qw/( )/),   greedy => 1);
Mdoc::def_macro( '.Qq',  gen_encloser(qw(" ")),   greedy => 1);
Mdoc::def_macro( '.Op',  gen_encloser(qw(@code{[ ]})), greedy => 1);
Mdoc::def_macro( '.Ql',  gen_encloser(qw(@quoteleft{} @quoteright{})),
    greedy => 1);
Mdoc::def_macro( '.Sq',  gen_encloser(qw(@quoteleft{} @quoteright{})),
    greedy => 1);
Mdoc::def_macro( '.Dq',  gen_encloser(qw(@quotedblleft{} @quotedblright{})), 
    greedy => 1);
Mdoc::def_macro( '.Eq', sub { 
        my ($o, $c) = (shift, pop); 
        gen_encloser($o, $c)->(@_) 
},  greedy => 1);
Mdoc::def_macro( '.D1', sub { "\@example\n", ns, @_, ns, "\n\@end example" },
    greedy => 1);
Mdoc::def_macro( '.Dl', sub { "\@example\n", ns, @_, ns, "\n\@end example" },
    greedy => 1);

Mdoc::def_macro( '.Oo',  gen_encloser(qw(@code{[ ]})), concat_until => '.Oc');
Mdoc::def_macro( 'Oo',   sub { '@code{[', ns, @_ } );
Mdoc::def_macro( 'Oc',   sub { @_, ns, pp(']}') } );

Mdoc::def_macro( '.Bro', gen_encloser(qw(@code{@{ @}})), concat_until => '.Brc');
Mdoc::def_macro( 'Bro',  sub { '@code{@{', ns, @_ } );
Mdoc::def_macro( 'Brc',  sub { @_, ns, pp('@}}') } );

Mdoc::def_macro( '.Po',  gen_encloser(qw/( )/), concat_until => '.Pc');
Mdoc::def_macro( 'Po',   sub { '(', @_     } );
Mdoc::def_macro( 'Pc',   sub { @_, ')' } );

Mdoc::def_macro( '.Ar', sub { mapwords {"\@kbd{$_}"} @_ } );
Mdoc::def_macro( '.Fl', sub { mapwords {"\@code{-$_}"} @_ } );
Mdoc::def_macro( '.Cm', sub { mapwords {"\@code{-$_}"} @_ } );
Mdoc::def_macro( '.Ic', sub { mapwords {"\@code{$_}"} @_ } );
Mdoc::def_macro( '.Cm', sub { mapwords {"\@code{$_}"} @_ } );
Mdoc::def_macro( '.Li', sub { mapwords {"\@code{$_}"} @_ } );
Mdoc::def_macro( '.Va', sub { mapwords {"\@code{$_}"} @_ } );
Mdoc::def_macro( '.Em', sub { mapwords {"\@emph{$_}"} @_ } );
Mdoc::def_macro( '.Fn', sub { '@code{'.(shift).'()}' } );
Mdoc::def_macro( '.Ss', sub { "\@subsubsection", hs, @_ });
Mdoc::def_macro( '.Sh', sub { 
        my $name = "@_"; 
        "\@node", hs, "$name\n", ns, "\@subsection", hs, $name
    });
Mdoc::def_macro( '.Ss', sub { "\@subsubsection", hs, @_ });
Mdoc::def_macro( '.Xr', sub { '@code{'.(shift).'('.(shift).')}', @_ } );
Mdoc::def_macro( '.Sx', gen_encloser(qw(@ref{ })) );
Mdoc::def_macro( '.Ux', sub { '@sc{unix}', @_ } );
Mdoc::def_macro( '.Fx', sub { '@sc{freebsd}', @_ } );
{
    my $name;
    Mdoc::def_macro('.Nm', sub {
        $name = shift || $ENV{AG_DEF_PROG_NAME} || 'XXX' if (!$name);
        "\@code{$name}"
    } );
}
Mdoc::def_macro( '.Pa', sub { mapwords {"\@file{$_}"} @_ } );
Mdoc::def_macro( '.Pp', sub { '' } );

# Setup references

Mdoc::def_macro( '.Rs', sub { "\@*\n", @_ } );
Mdoc::set_Re_callback(sub {
        my ($reference) = @_;
        "@*\n", ns, $reference->{authors}, ',', "\@emph{$reference->{title}}",
        ',', $reference->{optional}
    });

# Set up Bd/Ed

my %displays = (
    literal => [ '@verbatim', '@end verbatim' ],
);

Mdoc::def_macro( '.Bd', sub {
        (my $type = shift) =~ s/^-//;
        die "Not supported display type <$type>" 
            if not exists $displays{ $type };

        my $orig_ed = Mdoc::get_macro('.Ed');
        Mdoc::def_macro('.Ed', sub {
                Mdoc::def_macro('.Ed', delete $orig_ed->{run}, %$orig_ed);
                $displays{ $type }[1];
            });
        $displays{ $type }[0]
    });
Mdoc::def_macro('.Ed', sub { die '.Ed used but .Bd was not seen' });

# Set up Bl/El

my %lists = (
    bullet => [ '@itemize @bullet', '@end itemize' ],
    tag    => [ '@table @asis', '@end table' ],
    column => [ '@table @asis', '@end table' ],
);

Mdoc::set_Bl_callback(sub {
        my $type = shift;
        die "Specify a list type"             if not defined $type;
        $type =~ s/^-//;
        die "Not supported list type <$type>" if not exists $lists{ $type };
        Mdoc::set_El_callback(sub { $lists{ $type }[1] });
        $lists{ $type }[0]
    });
Mdoc::def_macro('.It', sub { '@item', hs, @_ });

for (qw(Aq Bq Brq Pq Qq Ql Sq Dq Eq Ar Fl Ic Pa Op Cm Li Fx Ux Va)) {
    my $m = Mdoc::get_macro(".$_");
    Mdoc::def_macro($_, delete $m->{run}, %$m);
}

sub print_line {
    my $s = shift;
    $s =~ s/\\&//g;
    print "$s\n";
}

sub preprocess_args {
    $_ =~ s/([{}])/\@$1/g for @_;
}

sub run {
    while (my ($macro, @args) = Mdoc::parse_line(\*STDIN, \&print_line, 
            \&preprocess_args)
    ) {
        my @ret = Mdoc::call_macro($macro, @args);
        if (@ret) {
            my $s = Mdoc::to_string(@ret);
            print_line($s);
        }
    }
    return 0;
}

exit run(@ARGV) unless caller;
