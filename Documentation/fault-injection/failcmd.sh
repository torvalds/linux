#!/bin/bash

echo 1 > /proc/self/make-it-fail
exec $*
