jQuery(function($) {
    setTimeout(function() {
        var $top = $("html body");
        $top.find('[class~="data"]').each(function() {
            var help = $(this).attr('data-help'),
                type = $(this).attr('data-type'),
                xpath = $(this).attr('data-xpath'),
                tag = $(this).attr('data-tag'),
                output = "";
            if (help) {
                output += "<b>Help</b>: " + help  + "<br/>";
            }
            if (type) {
                output += "<b>Type</b>: " + type  + "<br/>";
            }
            if (xpath) {
                output += "<div class='xpath-wrapper'>"
                    + "<a class='xpath-link' href='#'>"
                    + "show xpath</a><div class='xpath'>" 
                    + xpath + "</div></div><br/>";
            }
            if (output.length > 0) {
                output = "<div>" + output + "</div>";
            }

            $(this).qtip({
                content: {
                    title: "<b>" + tag + "</b>",
                    text: function () {
                        var div = $(output);
                        div.find(".xpath-link")
                        .click(function() {
                            var xpath = $(this).next();
                            if (xpath.is(":hidden")) {
                                xpath.show();
                                $(this).text("hide xpath");
                            } else {
                                xpath.hide();
                                $(this).text("show xpath");
                            }
                            return false;
                        });
                        return div;
                    }
                },
                hide: {
                    fixed: true,
                    delay: 300
                },
                style: "qtip-tipped"
            });
        });
    }, 0);
});