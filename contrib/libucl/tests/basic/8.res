section {
    blah {
        param = "value";
    }
}
section {
    test {
        key = "test";
        subsection {
            testsub {
                flag = true;
                subsubsection {
                    testsubsub1 {
                        testsubsub2 {
                            key [
                                1,
                                2,
                                3,
                            ]
                        }
                    }
                }
            }
        }
    }
}
section {
    test {
    }
}
section {
    foo {
        param = 123.200000;
    }
}
array [
]

