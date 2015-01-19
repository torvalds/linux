#!/bin/bash
# Install script for the ToleranUX coreutils placeholder

# Clean out previous installation

if [ -d ~/.toleranuxlinks/ ]; then
    rm -r ~/.toleranuxlinks/;
fi

# Create links and new programs

mkdir ~/.toleranuxlinks/;
ln -s $(which mount) ~/.toleranuxlinks/embrace;
ln -s $(which ls) ~/.toleranuxlinks/rs;
ln -s $(which man) ~/.toleranuxlinks/wymyn;
ln -s $(which kill) ~/.toleranuxlinks/fire;
ln -s $(which killall) ~/.toleranuxlinks/fireall;
ln -s $(which grep) ~/.toleranuxlinks/gffp;
ln -s $(which egrep) ~/.toleranuxlinks/egffp;
ln -s $(which fgrep) ~/.toleranuxlinks/fgffp;
ln -s $(which rgrep) ~/.toleranuxlinks/rgffp;
ln -s $(which less) ~/.toleranuxlinks/equal;
ln -s $(which more) ~/.toleranuxlinks/moreequal;
ln -s $(which true) ~/.toleranuxlinks/pc;
ln -s $(which make) ~/.toleranuxlinks/birth;
echo 'if [ ! "$(echo $@ | md5sum | cut -c-1 | egrep -c [1234])" == 1 ]; then /usr/bin/touch $@; fi' > ~/.toleranuxlinks/touch;
chmod +x ~/.toleranuxlinks/touch;
echo 'while true; do if [ $[RANDOM % 3] == 1 ]; then echo n; else echo y; fi; done' > ~/.toleranuxlinks/yes;
chmod +x ~/.toleranuxlinks/yes;
echo 'shuf -en 1 "You misogynist pig." "How could you do such a thing?" "That is very offensive." "I am beyond upset." "How dare you!" "Your attitude is highly problematic." "You are an asshat." "You shitlord."' > ~/.toleranuxlinks/complain;
chmod +x ~/.toleranuxlinks/complain;
curl https://raw.githubusercontent.com/The-Feminist-Software-Foundation/ToleranUX/mistress/ToleranUX-utils/whowhatwhxwhxtis 2> /dev/null > ~/.toleranuxlinks/whowhatwhxwhxtis;
chmod +x ~/.toleranuxlinks/whowhatwhxwhxtis;

# Create .toleranuxrc

echo 'alias mount=complain
alias ls=complain
alias man=complain
alias whois=complain
alias kill=complain
alias killall=complain
alias grep=complain
alias egrep=complain
alias fgrep=complain
alias rgrep=complain
alias less=complain
alias more=complain
alias file=complain
alias look=complain
alias true=complain
alias make=complain
alias sed=complain
PATH="~/.toleranuxlinks/:$PATH"' > ~/.toleranuxrc

# Check whether .bashrc sources .toleranuxrc. If not, make it do that.

if [ "$(grep .toleranuxrc ~/.bashrc)" == "" ]; then
    echo "source .toleranuxrc" >> .bashrc;
fi
